#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Reflect/Reflection.h"

MSTRUCT()
struct SServiceRegistryConfig
{
    MPROPERTY(Meta=(Cli="--listen"))
    uint16 ListenPort = 18000;

    // 心跳超时阈值：LastHeartbeatMs 距今差值超过此值（毫秒）则翻 unhealthy。
    MPROPERTY(Meta=(Cli="--heartbeat-timeout-ms"))
    uint32 HeartbeatTimeoutMs = 15000;

    // 完全失联阈值：超过此值则从 Endpoints_ 移除。
    MPROPERTY(Meta=(Cli="--evict-after-ms"))
    uint32 EvictAfterMs = 30000;

    // 日志：ServiceMain::Run 会读这些字段初始化 MLog 管道。
    MPROPERTY(Meta=(Cli="--log-file"))
    MString LogFilePath = "";

    MPROPERTY(Meta=(Cli="--log-rotate-bytes"))
    uint64 LogRotateBytes = 100ull * 1024ull * 1024ull;

    MPROPERTY(Meta=(Cli="--log-archives"))
    uint32 LogArchives = 5;

    MPROPERTY(Meta=(Cli="--log-config"))
    MString LogConfigPath = "";
};