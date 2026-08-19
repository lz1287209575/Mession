#pragma once

#include "Common/Runtime/Log/LogLevel.h"
#include "Common/Runtime/Log/LogRecord.h"
#include "Common/Runtime/Log/LogSinks.h"
#include "Common/Runtime/MLib.h"
#include <mutex>

// MConsoleSink — new-pipeline (spec §5.8) console sink. Writes serialized
// text lines to stdout via std::cout. Records are dropped by MinLevel
// at the writer; each accepted record becomes one line of the form:
//
//   [ISO-timestamp] [LEVEL] [CatName] [file:line] message\n
//
// `bUseColor` is reserved for a future ANSI-color pass-through; the first
// revision writes plain text regardless. The option is exposed as a
// public field so callers can flip it without a setter round-trip.
//
// Open/Close are no-ops for stdout (the libc stream is always available);
// they are still implemented to satisfy the ILogSink lifecycle contract.
class MConsoleSink : public ILogSink {
    public:
    MConsoleSink() = default;

    // New pipeline
    bool      Open() override;
    void      Close() override;
    void      WriteBatch(TSpan<const SLogRecord> Batch, TSpanMutable<char> OutBuffer) override;
    void      Flush() override;
    ELogLevel MinLevel() const override {
        return MinLevelValue;
    }
    const char* Name() const override {
        return "console";
    }

    // Configuration
    void SetMinLevel(ELogLevel Level) {
        MinLevelValue = Level;
    }
    void SetUseColor(bool bEnable) {
        bUseColor = bEnable;
    }

    private:
    ELogLevel  MinLevelValue = ELogLevel::Trace;
    bool       bUseColor     = false;
    bool       bOpen         = false;
    std::mutex WriteMutex;
};
