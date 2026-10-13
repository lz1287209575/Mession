/**
 * @file LogConfig.h
 * @brief MLog 门面配置结构体与 JSON 配置文件加载(规范 §5.9)。
 */
#pragma once
#include "Common/Runtime/Log/LogLevel.h"
#include "Common/Runtime/MLib.h"
#include <cstdarg>


struct SLogInitParams {
    MString   ConfigPath;
    ELogLevel GlobalDefaultLevel = ELogLevel::Info;
    bool      bUseColor          = false;
    MString   LogDir             = "Logs";
    size_t    RotatedFileBytes   = 100ull * 1024ull * 1024ull; // 100MB
    size_t    NumArchives        = 5;
    bool      bEnableUdp         = false;
    MString   UdpTarget; // "ip:port"
    bool      bEnableTcp = false;
    MString   TcpTarget; // "ip:port"

    // 控制台开关(默认开)。Console 始终可进程内使用;只发远端
    // sink 时可关掉。
    bool bEnableConsole = true;
    // 滚动文件路径;为空则不配置文件 sink。
    MString FilePath; // e.g. "Logs/echo.jsonl"
                      // 环形缓冲容量(条数)。默认 1M 条 ≈ 64MB(按 64B/条估算)。
    size_t RingCapacity = 1u << 16;
};

struct SLogCategoryConfig {
    MString   Name;
    ELogLevel Level       = ELogLevel::Info;
    bool      bSuppressed = false;
};

struct SLogRouteConfig {
    MString          Name;
    TVector<MString> Sinks; // "console", "file", "udp", "tcp", "coredump"
    ELogLevel        MinLevel = ELogLevel::Trace;
};

/**
 * @brief 把 JSON 配置文件就地应用到初始化参数与分类 / 路由列表。
 * @param Path 配置文件路径
 * @param InOutParams 初始化参数(就地修改)
 * @param OutCategories 输出的分类规则列表
 * @param OutRoutes 输出的路由规则列表
 * @return 文件存在且解析成功返回 true;文件缺失或解析失败返回
 * false 且不改动任何入参
 */
bool MLogApplyConfigFile(const MString& Path, SLogInitParams& InOutParams, TVector<SLogCategoryConfig>& OutCategories, TVector<SLogRouteConfig>& OutRoutes);