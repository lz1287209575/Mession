#include "Common/Runtime/Log/SinkWriter.h"

#include <chrono>

namespace
{
    long long NowSteadyMs()
    {
        using namespace std::chrono;
        return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    }
}

MLogSinkWriter::~MLogSinkWriter()
{
    Stop();
}

void MLogSinkWriter::SetSink(ILogSink* InSink, EFlushPolicy InPolicy)
{
    Sink   = InSink;
    Policy = InPolicy;
}

void MLogSinkWriter::EnqueueBatch(TSpan<const SLogRecord> Batch)
{
    if (Batch.empty()) return;
    if (Sink == nullptr) return;

    auto H = MakeShared<SSinkBatch>();
    H->Records.reserve(Batch.size());
    for (const auto& R : Batch) H->Records.push_back(R);

    {
        std::unique_lock<std::mutex> Lock(InboxMutex);
        // Bounded backpressure: spin/yield until the inbox drains below
        // the cap. Bounded by kMaxBatches so a stuck sink can't OOM us.
        while (Inbox.size() >= kMaxBatches)
        {
            InboxCv.wait_for(Lock, std::chrono::milliseconds(1));
        }
        Inbox.push(std::move(H));
    }
    InboxCv.notify_one();
}

void MLogSinkWriter::Start()
{
    if (bRunning.exchange(true)) return;
    Worker = std::thread([this]() { RunLoop(); });
}

void MLogSinkWriter::Stop()
{
    if (!bRunning.exchange(false)) return;
    InboxCv.notify_all();
    if (Worker.joinable()) Worker.join();
    // Drain anything left in the inbox (in case Stop() raced a producer).
    FlushSync();
}

void MLogSinkWriter::FlushSync()
{
    if (Sink == nullptr) return;
    // Pull & dispatch the inbox without entering Wait on the cv.
    TSharedPtr<SSinkBatch> Batch;
    while (true)
    {
        {
            std::lock_guard<std::mutex> Lock(InboxMutex);
            if (Inbox.empty()) break;
            Batch = std::move(Inbox.front());
            Inbox.pop();
        }
        if (Batch && !Batch->Records.empty())
        {
            // For FlushSync callers we don't have a provided scratch buffer —
            // reuse a thread-local. Sinks that need a real call-site buffer
            // (ConsoleSink / RollingFileSink) allocate one internally as
            // needed. We pass an empty span to signal "use the sink's
            // internal buffer" if it supports that mode; otherwise the sink
            // falls back to a per-call allocation.
            char StackBuf[4096] = {};
            TSpanMutable<char> Scratch(StackBuf, sizeof(StackBuf));
            TSpan<const SLogRecord> Recs(Batch->Records.data(), Batch->Records.size());
            Sink->WriteBatch(Recs, Scratch);
        }
    }
    Sink->Flush();
}

void MLogSinkWriter::RunLoop()
{
    LastFlushMs = NowSteadyMs();
    while (bRunning.load(std::memory_order_acquire))
    {
        TSharedPtr<SSinkBatch> Batch;
        {
            std::unique_lock<std::mutex> Lock(InboxMutex);
            if (Inbox.empty())
            {
                // Wait up to FlushIntervalMs for new data; on timeout
                // flush if Interval/IntervalOrSize policy wants it.
                InboxCv.wait_for(Lock, std::chrono::milliseconds(FlushIntervalMs));
                if (Inbox.empty())
                {
                    MaybeFlush(false);
                    continue;
                }
            }
            Batch = std::move(Inbox.front());
            Inbox.pop();
        }
        InboxCv.notify_one();  // Producer backpressure slot freed
        if (!Batch || Batch->Records.empty()) continue;

        char StackBuf[4096] = {};
        TSpanMutable<char> Scratch(StackBuf, sizeof(StackBuf));
        TSpan<const SLogRecord> Recs(Batch->Records.data(), Batch->Records.size());
        Sink->WriteBatch(Recs, Scratch);
        BufferedBytes += static_cast<int>(Scratch.size() > 0 ? Scratch.size() : 0);
        MaybeFlush(false);
    }
    // Drain on exit — pull remaining batches and flush the sink.
    FlushSync();
}

void MLogSinkWriter::MaybeFlush(bool bForce)
{
    if (Sink == nullptr) return;
    const long long Now = NowSteadyMs();
    const bool bInterval = Policy == EFlushPolicy::Interval
        || (Policy == EFlushPolicy::IntervalOrSize
            && (Now - LastFlushMs) >= FlushIntervalMs);
    const bool bSize = Policy == EFlushPolicy::SizeThreshold
        || (Policy == EFlushPolicy::IntervalOrSize && BufferedBytes >= FlushSizeThreshold);
    if (bForce || bInterval || bSize)
    {
        Sink->Flush();
        LastFlushMs = Now;
        BufferedBytes = 0;
    }
}