#pragma once
#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Log/LogLevel.h"
#include "Common/Runtime/Log/LogRecord.h"
#include "Common/Runtime/Log/LogSinks.h"

#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>

// MLogSinkWriter — one writer thread per ILogSink (spec §5.7).
//
// Producer side: MLogDispatcher pushes batches via EnqueueBatch() (lock-free
// path: we hand the batch to a small MPSC inbox protected by a mutex + cv —
// not lock-free because ILogSink is not move-constructible in general, so we
// can only move a TSinkBatch wrapper when the inbox is small). The hot-path
// for the dispatcher is EnqueueBatch; the lock contention surface is the
// inbox mutex, not the sink itself.
//
// Consumer side: RunLoop() pulls batches, applies the configured
// EFlushPolicy, and calls Sink->WriteBatch. Sink owns its own per-sink
// serialization (text for Console, JSON Lines for the file/UDP/TCP sinks),
// so the writer thread does not need to know the wire format.
//
// Lifecycle: Start() launches the thread; Stop() sets bRunning=false,
// notifies the cv, and joins. After Stop() returns, the inbox is empty
// and no further Sink->* calls will happen on this writer thread.
class MLogSinkWriter
{
public:
    struct SSinkBatch
    {
        TVector<SLogRecord> Records;
    };

    MLogSinkWriter() = default;
    ~MLogSinkWriter();

    // Bind to a sink. Must be called before Start(). SinkWriter does NOT
    // own the sink; the caller must keep it alive until Stop() returns.
    void SetSink(ILogSink* InSink, EFlushPolicy InPolicy = EFlushPolicy::IntervalOrSize);

    // Hot-path entry. Hands the batch to the writer thread. Blocks on the
    // inbox if it is already full (small bounded queue — 16 batches), which
    // is a documented backpressure signal.
    void EnqueueBatch(TSpan<const SLogRecord> Batch);

    void Start();
    void Stop();

    void SetFlushIntervalMs(int Ms)   { FlushIntervalMs = Ms; }
    void SetFlushSizeThreshold(int B) { FlushSizeThreshold = B; }

    // Drain the inbox synchronously and run the sink until empty. Used by
    // MLog::Shutdown() so that critical-path records make it to the wire
    // before the writer thread exits.
    void FlushSync();

private:
    // Maximum number of batches the inbox holds before EnqueueBatch blocks.
    // Sized so we balance wake-up latency against writer-thread pressure.
    static constexpr size_t kMaxBatches = 16;

    ILogSink* Sink = nullptr;
    EFlushPolicy Policy = EFlushPolicy::IntervalOrSize;
    int FlushIntervalMs = 10;
    int FlushSizeThreshold = 64 * 1024;

    std::thread Worker;
    std::mutex  InboxMutex;
    std::condition_variable InboxCv;
    TQueue<TSharedPtr<SSinkBatch>> Inbox;
    std::atomic<bool> bRunning{false};

    // Bytes accumulated since last Flush(); used by SizeThreshold / IntervalOrSize.
    int BufferedBytes = 0;
    long long LastFlushMs = 0;

    void RunLoop();
    void MaybeFlush(bool bForce);
};