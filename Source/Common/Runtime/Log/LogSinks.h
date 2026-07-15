#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Log/LogLevel.h"
#include "Common/Runtime/Log/LogRecord.h"

// =============================================================================
// ILogSink — the new-pipeline sink contract (spec §5.8).
// =============================================================================
//
// Lifecycle / per-batch:
//   Open()        — prepare the sink (open file, allocate buffer, etc.). Default
//                   returns true so sinks that need no setup (e.g. an in-memory
//                   ring) need not override it.
//   Close()       — release resources. Default no-op.
//   WriteBatch()  — consume a pre-populated buffer of SLogRecord plus a caller-
//                   provided scratch `OutBuffer`; serialize into `OutBuffer` and
//                   emit to the underlying stream. Pure-virtual: every new-
//                   pipeline sink must implement its serialization. Sinks that
//                   are only used through the legacy MLogger path (the existing
//                   MConsoleSink / MFileSink) override the legacy `Write()`
//                   method instead and inherit the empty default here.
//   Flush()       — push pending bytes to the OS. Default no-op.
//
// Per-record gating / introspection:
//   MinLevel()    — records below this level are dropped at the writer (default
//                   Trace, i.e. accept everything).
//   Name()        — short identifier for log-routing / metrics labeling.
//                   Default returns "".
//
// Legacy bridge (kept for MLogger compatibility — see ConsoleLogSink.h and
// FileLogSink.h, which inherit from ILogSink and override Write() to integrate
// with the existing MLogger plumbing):
//   Write(ELogLevel, const MString&) — default no-op.
//   GetMinLevel()                    — default returns MinLevel().
//
// The legacy methods have non-pure defaults so the existing MConsoleSink /
// MFileSink (still used by MLogger) remain concrete. They are scheduled for
// removal when MLogger itself is migrated to the new pipeline (spec §12 step 5).
class ILogSink
{
public:
    virtual ~ILogSink() = default;

    // --- New pipeline (spec §5.8) ---
    virtual bool Open() { return true; }
    virtual void Close() {}
    // WriteBatch is intentionally non-pure: legacy sinks that live on the
    // MLogger path (ConsoleLogSink / FileLogSink) keep working without
    // having to implement the new contract. New-pipeline sinks (ConsoleSink
    // / RollingFileSink) override it.
    //
    // Note on the buffer type: the brief's snippet shows `TSpan<char>` but
    // the project's TSpan alias is `std::span<const T>`, which would make
    // the buffer read-only. The intent ("sink fills it with serialized text
    // and writes to its underlying stream") requires a mutable scratch
    // buffer, so we use `TSpanMutable<char>` = `std::span<char>` here. The
    // caller is still expected to provide the storage.
    virtual void WriteBatch(TSpan<const SLogRecord> /*Batch*/, TSpanMutable<char> /*OutBuffer*/) {}
    virtual void Flush() {}
    virtual ELogLevel MinLevel() const { return ELogLevel::Trace; }
    virtual const char* Name() const { return ""; }

    // --- Legacy bridge for the still-present MLogger path ---
    virtual void Write(ELogLevel /*Level*/, const MString& /*FormattedLine*/) {}
    virtual ELogLevel GetMinLevel() const { return MinLevel(); }
};

// =============================================================================
// ELogSinkId — bit positions in the 32-bit SLogRecord::SinkMask / SLogRouteRule
// routing table. Lifted from LogRouter.h (Task 4) so the enum lives next to the
// interface that consumes it. Bit values 0..4 are STABLE: do not reorder.
// =============================================================================
enum class ELogSinkId : uint32
{
    Console  = 0,   // 1u << 0
    File     = 1,   // 1u << 1
    Udp      = 2,   // 1u << 2
    Tcp      = 3,   // 1u << 3
    Coredump = 4,   // 1u << 4
};

inline uint32 MakeSinkMask(ELogSinkId Id)
{
    return 1u << static_cast<uint32>(Id);
}
