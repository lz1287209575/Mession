#pragma once

#include "Common/Runtime/Log/LogLevel.h"
#include "Common/Runtime/Log/LogRecord.h"
#include "Common/Runtime/Log/LogSinks.h"
#include "Common/Runtime/MLib.h"

#include <atomic>
#include <mutex>

// MUdpSink — new-pipeline (spec §5.8) UDP sink. Serializes records as JSON
// Lines (each record on its own '\n'-terminated line) and emits them as
// datagrams. If a batch would exceed the path MTU, records are split across
// multiple packets (one or more records per packet, never partial records).
//
// `Target` is a dotted-quad "ip:port" string (e.g. "127.0.0.1:9999"). Open()
// resolves it once and stashes the sockaddr_in; Close() drops the socket.
// WriteBatch() holds WriteMutex briefly to serialize the sockaddr access
// then issues sendto() outside the lock; the producer thread that called
// WriteBatch is the only one that touches the socket, so we do not need
// multi-producer synchronization here.
//
// Flush() is a no-op: UDP has no flush semantics and the per-record
// sendto() already pushes to the kernel immediately.
class MUdpSink : public ILogSink {
    public:
    MUdpSink();
    ~MUdpSink() override;

    // New pipeline
    bool      Open() override;
    void      Close() override;
    void      WriteBatch(TSpan<const SLogRecord> Batch, TSpanMutable<char> OutBuffer) override;
    void      Flush() override;
    ELogLevel MinLevel() const override {
        return MinLevelValue;
    }
    const char* Name() const override {
        return "udp";
    }

    // Configuration. Must be set before Open().
    MString Target;                       // "ip:port"
    int     MaxDatagramBytes = 63 * 1024; // safety cap; typical MTU is ~1400

    void SetMinLevel(ELogLevel Level) {
        MinLevelValue = Level;
    }

    private:
    ELogLevel  MinLevelValue = ELogLevel::Trace;
    int        SockFd        = -1;
    bool       bOpen         = false;
    std::mutex WriteMutex;
};