#pragma once

#include "Common/Runtime/Log/LogLevel.h"
#include "Common/Runtime/Log/LogRecord.h"
#include "Common/Runtime/Log/LogSinks.h"
#include "Common/Runtime/MLib.h"

#include <atomic>
#include <mutex>

// MTcpSink — new-pipeline (spec §5.8) TCP sink. Maintains a single TCP
// connection to `Target` and streams JSON Lines down it. On connection
// loss (send() returns ECONNRESET or EPIPE), the sink enters a 5s backoff
// and attempts one reconnect; records emitted during the disconnect
// window are counted as dropped via MLogMetrics.
//
// WriteBatch() holds WriteMutex briefly to guard the connected-state
// transitions. The producer thread that called WriteBatch owns the
// in-flight send() — sendto is *not* thread-safe across concurrent
// callers on the same fd for TCP.

class MTcpSink : public ILogSink {
    public:
    MTcpSink();
    ~MTcpSink() override;

    // New pipeline
    bool      Open() override;
    void      Close() override;
    void      WriteBatch(TSpan<const SLogRecord> Batch, TSpanMutable<char> OutBuffer) override;
    void      Flush() override;
    ELogLevel MinLevel() const override {
        return MinLevelValue;
    }
    const char* Name() const override {
        return "tcp";
    }

    // Configuration. Must be set before Open().
    MString Target; // "ip:port"
    int     ReconnectBackoffMs = 5000;

    void SetMinLevel(ELogLevel Level) {
        MinLevelValue = Level;
    }

    private:
    ELogLevel MinLevelValue = ELogLevel::Trace;
    int       SockFd        = -1;
    bool      bOpen         = false;

    // Tracks when the next reconnect attempt is allowed. Held under WriteMutex.
    long long NextReconnectAtMs = 0;

    std::mutex WriteMutex;

    // Issue a blocking send of `Bytes` starting at `Ptr`. On error, drop the
    // connection (CloseLocked) so the next WriteBatch triggers a reconnect.
    void SendLocked(const char* Ptr, size_t Bytes);

    // Drop the socket. Caller must hold WriteMutex.
    void CloseLocked();
};