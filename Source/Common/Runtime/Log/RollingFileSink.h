#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Log/LogLevel.h"
#include "Common/Runtime/Log/LogRecord.h"
#include "Common/Runtime/Log/LogSinks.h"
#include <cstdio>
#include <mutex>

// MRollingFileSink — new-pipeline (spec §5.8) rolling JSON-Lines file sink.
//
// Each Open() opens `FilePath` for append. WriteBatch serializes every record
// as one JSON object and writes them (plus '\n' separators) via fwrite —
// the bulk-path that turns the entire batch into one syscall on Unix.
//
// Rotation policy: when the current file's on-disk size plus the size of the
// next record would exceed `RotatedFileBytes`, the current file is closed,
// renamed with a numeric suffix, the oldest archive beyond `NumArchives` is
// deleted, and a fresh file is opened. Rotation is per-record (not per-line)
// so a single oversized record can never spill across the threshold by more
// than one record's worth of bytes.
//
// Flush() calls fflush() on every call, and additionally issues an
// fdatasync() every `FlushesPerFsync` calls (default 10) so durability and
// throughput stay balanced.
//
// `ServiceName` is included in the JSON payload and (when rotated) in the
// archive name. `FilePath` is the live log path; archives get a numeric
// suffix and stay in the same directory.
class MRollingFileSink : public ILogSink
{
public:
    MRollingFileSink();
    ~MRollingFileSink() override;

    // New pipeline
    bool Open() override;
    void Close() override;
    void WriteBatch(TSpan<const SLogRecord> Batch, TSpanMutable<char> OutBuffer) override;
    void Flush() override;
    ELogLevel MinLevel() const override { return MinLevelValue; }
    const char* Name() const override { return "file"; }

    // Configuration (public fields for ease of integration; setter-style
    // access via member assignment is the documented style — see EchoService
    // examples). Must be set before Open().
    MString ServiceName;            // e.g. "GatewayServer"
    MString FilePath;               // e.g. "/tmp/test-mession-log.jsonl"
    size_t  RotatedFileBytes = 100ull * 1024ull * 1024ull;  // 100MB default
    size_t  NumArchives      = 5;
    uint32  FlushesPerFsync  = 10;

    void SetMinLevel(ELogLevel Level) { MinLevelValue = Level; }

private:
    ELogLevel   MinLevelValue = ELogLevel::Trace;
    FILE*       Stream        = nullptr;
    size_t      CurrentBytes  = 0;
    uint32      FlushCount    = 0;
    bool        bOpen         = false;
    int         Fd            = -1;
    std::mutex  WriteMutex;

    // Rotate: close Stream, rename to <path>.<N>, delete oldest if N exceeds
    // NumArchives, then reopen Stream. Called under WriteMutex.
    void RotateLocked();

    // Open Stream and seed CurrentBytes from the existing on-disk size
    // (the file is append-only across runs by design). Called under
    // WriteMutex.
    bool OpenLocked();

    // Issue fdatasync on the underlying fd when the counter reaches the
    // threshold. Called from Flush() under WriteMutex.
    void MaybeFsyncLocked();
};
