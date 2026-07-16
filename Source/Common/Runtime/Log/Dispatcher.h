#pragma once
#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Log/LogRecord.h"
#include "Common/Runtime/Log/MpscRingBuffer.h"

#include <atomic>
#include <thread>
#include <vector>

class MLogSinkWriter;

// MLogDispatcher — single-consumer thread (spec §5.6) sitting between the
// shared MPSC ring buffer and the per-sink writers. Pops records in
// batches, partitions by SinkMask, and hands each partition to the right
// MLogSinkWriter.
//
// Rationale for partition-on-read vs partition-on-write: the producer call
// site needs to enqueue with as little locking as possible. Partitioning on
// read keeps enqueue to one atomic slot reservation. The cost — copying
// records into per-sink scratch — is paid by exactly one thread (this one)
// so we don't add contention to the hot path.
class MLogDispatcher
{
public:
    MLogDispatcher() = default;
    ~MLogDispatcher();

    // Wire the dispatcher to its input ring buffer and the set of writers.
    // References must outlive the dispatcher; storage ownership is external.
    void Configure(TMpscRingBuffer<SLogRecord>* InQueue,
                   TVector<MLogSinkWriter*>* InWriters);

    void Start();
    void Stop();

    // Optional: resize the partition scratch. Defaults to 256 entries.
    void SetBatchSize(size_t InBatchSize) { BatchSize = InBatchSize; }

private:
    TMpscRingBuffer<SLogRecord>* Queue = nullptr;
    TVector<MLogSinkWriter*>*    Writers = nullptr;
    size_t BatchSize = 256;

    std::thread Worker;
    std::atomic<bool> bRunning{false};

    void RunLoop();
};