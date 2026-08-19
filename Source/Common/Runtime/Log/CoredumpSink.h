#pragma once
#include "Common/Runtime/Log/LogLevel.h"
#include "Common/Runtime/Log/LogRecord.h"
#include "Common/Runtime/Log/LogSinks.h"
#include "Common/Runtime/MLib.h"

#include <cstdarg>
#include <functional>

// MCoredumpSink — synchronous FATAL capture (spec §6.1).
//
// Triggered from MLog::HandleFatal: bypasses the async ring buffer,
// scans the buffer's tail (last N records), collects process + system
// info, writes a JSON Lines dump to Logs/coredump/<service>-<ts>.jsonl,
// and then raises SIGABRT so the OS writes a core file as well.
//
// We do NOT depend on the application stack to call backtrace() — by the
// time HandleFatal runs the stack may already be partly unwound. The OS
// coredump captures the full stack; our job is to capture the log tail
// + process context, not to walk the stack itself.
//
// SConfig::UploadHook is reserved (spec §6.2). When set, OnDumpComplete
// fires the hook on a detached std::thread so the main path stays
// synchronous. If the upload fails the error is written to stderr only.
class MCoredumpSink {
    public:
    struct SConfig {
        MString                                      DumpDir        = "Logs/coredump";
        size_t                                       RecentRecords  = 1000;
        bool                                         bForceCoreDump = true;
        std::function<void(const MString& DumpPath)> UploadHook;
    };

    static void HandleFatal(const SConfig& Config, const SLogRecord& TriggeringRecord, const TVector<SLogRecord>& TailRecords, const MString& TriggeringMessage);
};