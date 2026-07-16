#pragma once
#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Log/LogLevel.h"
#include <cstdarg>

// MLog facade configuration (spec §5.9).
struct SLogInitParams
{
    MString  ConfigPath;
    ELogLevel GlobalDefaultLevel = ELogLevel::Info;
    bool      bUseColor          = false;
    MString   LogDir             = "Logs";
    size_t    RotatedFileBytes   = 100ull * 1024ull * 1024ull;  // 100MB
    size_t    NumArchives        = 5;
    bool      bEnableUdp         = false;
    MString   UdpTarget;          // "ip:port"
    bool      bEnableTcp         = false;
    MString   TcpTarget;          // "ip:port"

    // Console on/off (default on). Console is always available in-process;
    // disable when redirecting to remote sinks only.
    bool      bEnableConsole     = true;
    // File roll path; if empty no file sink is configured.
    MString   FilePath;           // e.g. "Logs/echo.jsonl"
    // Ring buffer capacity (in records). Default 1M = 64MB at 64B/record.
    size_t    RingCapacity       = 1u << 20;
};

struct SLogCategoryConfig
{
    MString  Name;
    ELogLevel Level = ELogLevel::Info;
    bool      bSuppressed = false;
};

struct SLogRouteConfig
{
    MString Name;
    TVector<MString> Sinks;     // "console", "file", "udp", "tcp", "coredump"
    ELogLevel MinLevel = ELogLevel::Trace;
};

// Apply a JSON config file in-place to the params & category / route list.
// Returns true if the file existed and parsed; missing-file or parse-fail
// returns false WITHOUT mutating the inputs.
bool MLogApplyConfigFile(const MString& Path,
                         SLogInitParams& InOutParams,
                         TVector<SLogCategoryConfig>& OutCategories,
                         TVector<SLogRouteConfig>& OutRoutes);