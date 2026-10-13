/**
 * @file SinkWriter.cpp
 * @brief MLogSinkWriter 实现 —— 单 sink 的写线程、有界收件箱与刷新策略。
 */
#include "Common/Runtime/Log/SinkWriter.h"

#include <chrono>

namespace {
    long long NowSteadyMs() {
        using namespace std::chrono;
        return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    }
} // namespace

MLogSinkWriter::~MLogSinkWriter() {
    Stop();
}

void MLogSinkWriter::SetSink(ILogSink* InSink, EFlushPolicy InPolicy) {
    Sink   = InSink;
    Policy = InPolicy;
}

void MLogSinkWriter::EnqueueBatch(TSpan<const SLogRecord> Batch) {
    if (Batch.empty())
        return;
    if (Sink == nullptr)
        return;

    auto H = MakeShared<SSinkBatch>();
    H->Records.reserve(Batch.size());
    for (const auto& R : Batch)
        H->Records.push_back(R);

    {
        std::unique_lock<std::mutex> Lock(InboxMutex);
        // 有界背压:等到收件箱降到上限以下为止。上限就是 kMaxBatches
        // (16 批),所以即使 sink 卡死也不会把内存吃穿。
        while (Inbox.size() >= kMaxBatches) {
            InboxCv.wait_for(Lock, std::chrono::milliseconds(1));
        }
        Inbox.push(std::move(H));
    }
    InboxCv.notify_one();
}

void MLogSinkWriter::Start() {
    if (bRunning.exchange(true))
        return;
    Worker = std::thread([this]() { RunLoop(); });
}

void MLogSinkWriter::Stop() {
    if (!bRunning.exchange(false))
        return;
    InboxCv.notify_all();
    if (Worker.joinable())
        Worker.join();
    // 排空收件箱残留:Stop() 可能与生产者并发,漏掉这些批次就是丢日志。
    FlushSync();
}

void MLogSinkWriter::FlushSync() {
    if (Sink == nullptr)
        return;
    // 直接取空收件箱,不进入 cv 等待。
    TSharedPtr<SSinkBatch> Batch;
    while (true) {
        {
            std::lock_guard<std::mutex> Lock(InboxMutex);
            if (Inbox.empty())
                break;
            Batch = std::move(Inbox.front());
            Inbox.pop();
        }
        if (Batch && !Batch->Records.empty()) {
            // FlushSync 的调用点没有可复用的缓冲区,这里给一个 4KB 栈缓冲
            // 兜底。需要更大缓冲的 sink(ConsoleSink / RollingFileSink)会在
            // 内部按需自行处理,不依赖这个缓冲的大小。
            char                    StackBuf[4096] = {};
            TSpanMutable<char>      Scratch(StackBuf, sizeof(StackBuf));
            TSpan<const SLogRecord> Recs(Batch->Records.data(), Batch->Records.size());
            Sink->WriteBatch(Recs, Scratch);
        }
    }
    Sink->Flush();
}

void MLogSinkWriter::RunLoop() {
    LastFlushMs = NowSteadyMs();
    while (bRunning.load(std::memory_order_acquire)) {
        TSharedPtr<SSinkBatch> Batch;
        {
            std::unique_lock<std::mutex> Lock(InboxMutex);
            if (Inbox.empty()) {
                // 最多等 FlushIntervalMs。超时后如果 Interval/IntervalOrSize
                // 策略认为该刷了,就走下面的 MaybeFlush(false)。
                InboxCv.wait_for(Lock, std::chrono::milliseconds(FlushIntervalMs));
                if (Inbox.empty()) {
                    MaybeFlush(false);
                    continue;
                }
            }
            Batch = std::move(Inbox.front());
            Inbox.pop();
        }
        // 腾出一个收件箱槽位,唤醒可能正在等待的生产者
        InboxCv.notify_one();
        if (!Batch || Batch->Records.empty())
            continue;

        char                    StackBuf[4096] = {};
        TSpanMutable<char>      Scratch(StackBuf, sizeof(StackBuf));
        TSpan<const SLogRecord> Recs(Batch->Records.data(), Batch->Records.size());
        Sink->WriteBatch(Recs, Scratch);
        BufferedBytes += static_cast<int>(Scratch.size() > 0 ? Scratch.size() : 0);
        MaybeFlush(false);
    }
    // 退出前排空:取完剩余批次并刷新 sink。
    FlushSync();
}

void MLogSinkWriter::MaybeFlush(bool bForce) {
    if (Sink == nullptr)
        return;
    const long long Now       = NowSteadyMs();
    const bool      bInterval = Policy == EFlushPolicy::Interval || (Policy == EFlushPolicy::IntervalOrSize && (Now - LastFlushMs) >= FlushIntervalMs);
    const bool      bSize     = Policy == EFlushPolicy::SizeThreshold || (Policy == EFlushPolicy::IntervalOrSize && BufferedBytes >= FlushSizeThreshold);
    if (bForce || bInterval || bSize) {
        Sink->Flush();
        LastFlushMs   = Now;
        BufferedBytes = 0;
    }
}