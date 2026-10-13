/**
 * @file Dispatcher.cpp
 * @brief MLogDispatcher 实现 —— 从 MPSC 环形队列批量取日志,按 SinkMask 扇出到各 sink 写线程。
 */
#include "Common/Runtime/Log/Dispatcher.h"
#include "Common/Runtime/Log/LogMetrics.h"
#include "Common/Runtime/Log/SinkWriter.h"

#include <chrono>

MLogDispatcher::~MLogDispatcher() {
    Stop();
}

void MLogDispatcher::Configure(TMpscRingBuffer<SLogRecord>* InQueue, TVector<MLogSinkWriter*>* InWriters) {
    Queue   = InQueue;
    Writers = InWriters;
}

void MLogDispatcher::Start() {
    if (bRunning.exchange(true))
        return;
    Worker = std::thread([this]() { RunLoop(); });
}

void MLogDispatcher::Stop() {
    if (!bRunning.exchange(false))
        return;
    if (Worker.joinable())
        Worker.join();
}

void MLogDispatcher::RunLoop() {
    TVector<SLogRecord>          Batch;
    TVector<TVector<SLogRecord>> Partitions;
    Batch.reserve(BatchSize);
    Partitions.reserve(8);
    TVector<SLogRecord> Empty;

    while (bRunning.load(std::memory_order_acquire)) {
        Batch.clear();
        Batch.resize(BatchSize);
        const size_t N = Queue->DequeueBatch(Batch.data(), BatchSize);
        if (N == 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(50));
            continue;
        }
        Batch.resize(N);

        // 预先按写线程数量扩好分区数组。每个写线程在 Partitions 中有固定
        // 下标,因此下面的逐条分发可以直接 append,无需每来一条记录就把
        // SinkMask 重新映射成下标。
        const size_t NumSinks = Writers ? Writers->size() : 0;
        if (Partitions.size() < NumSinks) {
            Partitions.resize(NumSinks);
        }
        for (auto& P : Partitions)
            P.clear();

        for (const SLogRecord& R : Batch) {
            const uint32 Mask = R.SinkMask;
            for (size_t i = 0; i < NumSinks; ++i) {
                if (Mask & (1u << static_cast<uint32>(i))) {
                    Partitions[i].push_back(R);
                }
            }
        }

        // 扇出:把每个分区交给对应的写线程。空分区直接跳过,空闲的 sink
        // 不应因此被唤醒。
        for (size_t i = 0; i < NumSinks; ++i) {
            if (Partitions[i].empty())
                continue;
            MLogSinkWriter* W = (*Writers)[i];
            if (W == nullptr)
                continue;
            TSpan<const SLogRecord> Span(Partitions[i].data(), Partitions[i].size());
            W->EnqueueBatch(Span);
        }

        MLogMetrics::IncDispatchedBatches();
    }
}