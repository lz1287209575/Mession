#include "Common/Runtime/Log/Dispatcher.h"
#include "Common/Runtime/Log/SinkWriter.h"
#include "Common/Runtime/Log/LogMetrics.h"

#include <chrono>

MLogDispatcher::~MLogDispatcher()
{
    Stop();
}

void MLogDispatcher::Configure(TMpscRingBuffer<SLogRecord>* InQueue,
                               TVector<MLogSinkWriter*>* InWriters)
{
    Queue   = InQueue;
    Writers = InWriters;
}

void MLogDispatcher::Start()
{
    if (bRunning.exchange(true)) return;
    Worker = std::thread([this]() { RunLoop(); });
}

void MLogDispatcher::Stop()
{
    if (!bRunning.exchange(false)) return;
    if (Worker.joinable()) Worker.join();
}

void MLogDispatcher::RunLoop()
{
    TVector<SLogRecord>     Batch;
    TVector<TVector<SLogRecord>> Partitions;
    Batch.reserve(BatchSize);
    Partitions.reserve(8);
    TVector<SLogRecord> Empty;

    while (bRunning.load(std::memory_order_acquire))
    {
        Batch.clear();
        Batch.resize(BatchSize);
        const size_t N = Queue->DequeueBatch(Batch.data(), BatchSize);
        if (N == 0)
        {
            std::this_thread::sleep_for(std::chrono::microseconds(50));
            continue;
        }
        Batch.resize(N);

        // Resize partitions to the writer count up front. Each writer has a
        // fixed index in the partitions array, so the per-record dispatch
        // below can append without re-hashing SinkMask → index.
        const size_t NumSinks = Writers ? Writers->size() : 0;
        if (Partitions.size() < NumSinks)
        {
            Partitions.resize(NumSinks);
        }
        for (auto& P : Partitions) P.clear();

        for (const SLogRecord& R : Batch)
        {
            const uint32 Mask = R.SinkMask;
            for (size_t i = 0; i < NumSinks; ++i)
            {
                if (Mask & (1u << static_cast<uint32>(i)))
                {
                    Partitions[i].push_back(R);
                }
            }
        }

        // Fan-out: hand each partition to its writer. Empty partitions are
        // skipped so an idle sink sees zero wakeups.
        for (size_t i = 0; i < NumSinks; ++i)
        {
            if (Partitions[i].empty()) continue;
            MLogSinkWriter* W = (*Writers)[i];
            if (W == nullptr) continue;
            TSpan<const SLogRecord> Span(Partitions[i].data(), Partitions[i].size());
            W->EnqueueBatch(Span);
        }

        MLogMetrics::IncDispatchedBatches();
    }
}