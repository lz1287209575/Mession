/**
 * @file Log.h
 * @brief MLog 门面(facade)API —— 日志系统的统一入口(规范 §5.9)。
 *
 * @detail 生命周期:进程启动时调用一次 Init();退出前调用 Shutdown()
 * 排空环形队列并 join 所有工作线程。
 *
 * 热路径:Write() 是所有日志调用点的入口。先做分类级过滤
 * (MLogFilter),再解析路由决策(SinkMask),最后把单条 SLogRecord
 * 压入无锁 MPSC 环形队列。调用方永远不会阻塞在磁盘或网络上——
 * 那些发生在 dispatcher 与 writer 线程。
 *
 * FATAL 路径:HandleFatal() 完全绕过环形队列,直接快照近期记录、
 * 调用 MCoredumpSink 落盘,然后抛 SIGABRT。这是唯一一条在调用方
 * 线程同步执行事情的路径。
 */
#pragma once
#include "Common/Runtime/Log/LogCategories.h"
#include "Common/Runtime/Log/LogCategory.h"
#include "Common/Runtime/Log/LogConfig.h"
#include "Common/Runtime/Log/LogLevel.h"
#include "Common/Runtime/Log/LogRecord.h"
#include "Common/Runtime/Log/LogSinks.h"
#include "Common/Runtime/Log/MpscRingBuffer.h"
#include "Common/Runtime/MLib.h"

#include <cstdarg>

class MLogSinkWriter;
class MLogDispatcher;


namespace MLog {
    // 启动:装配 sink、注册表、dispatcher 与写线程。幂等 —— 第二次调用是空操作。
    void Init(const SLogInitParams& Params);

    // 关闭:排空环形队列(5s 超时)并 join 所有工作线程。可重复调用。
    void Shutdown();

    // 热路径入口。Category 指针不持有所有权,必须是项目定义的
    // SLogCategory*(LogCore / LogNet 等),或 MLogRegistry::RegisterCategory
    // 返回的指针。
    void Write(const SLogCategory* Category, ELogLevel Level, const char* Fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
        __attribute__((format(printf, 3, 4)))
#endif
        ;
    void WriteV(const SLogCategory* Category, ELogLevel Level, const char* Fmt, va_list Args);

    // FATAL 同步路径。
    void HandleFatal(const SLogCategory* Category, const char* Fmt, va_list Args);

    // 运行时配置(级别 / 抑制开关)。
    void SetCategoryLevel(const SLogCategory& Category, ELogLevel Level);
    void SetCategorySuppressed(const SLogCategory& Category, bool bSuppressed);

    // 套用已解析的 JSON 配置。未按名覆盖的既有规则保持不动。
    // 至少成功应用一条时返回 true。
    bool ApplyCategoryConfig(const TVector<SLogCategoryConfig>& Configs);
    bool ApplyRouteConfig(const TVector<SLogRouteConfig>& Configs);

    // 排空 —— 主流程在 Shutdown 过重时于退出前调用。
    void Flush(int TimeoutMs = 1000);
} // namespace MLog

// 取代已删除的 MLogger::IsDebugBuild()(由 NDEBUG 决定)。
inline constexpr bool MLogIsDebugBuild() {
#if defined(NDEBUG)
    return false;
#else
    return true;
#endif
}

// ---- 宏(规范 §4.3) --------------------------------------------------------
//
// 项目级分类宏展开成 MLog::Write(LogXxx, ELogLevel::Level, ...)。
// 保留 5 个旧 LOG_* 宏,以免既有代码编译不过;它们映射到 LogCore 的
// 对应级别。
//
// LOG_FATAL_EX(Cat, ...) 是同步 coredump 路径。
#define NET_LOG(Level, Fmt, ...)   ::MLog::Write(LogNet, ::ELogLevel::Level, Fmt, __VA_ARGS__)
#define DB_LOG(Level, Fmt, ...)    ::MLog::Write(LogDb, ::ELogLevel::Level, Fmt, __VA_ARGS__)
#define RPC_LOG(Level, Fmt, ...)   ::MLog::Write(LogRpc, ::ELogLevel::Level, Fmt, __VA_ARGS__)
#define AUTH_LOG(Level, Fmt, ...)  ::MLog::Write(LogAuth, ::ELogLevel::Level, Fmt, __VA_ARGS__)
#define SCENE_LOG(Level, Fmt, ...) ::MLog::Write(LogScene, ::ELogLevel::Level, Fmt, __VA_ARGS__)
#define CORE_LOG(Level, Fmt, ...)  ::MLog::Write(LogCore, ::ELogLevel::Level, Fmt, __VA_ARGS__)

#define LOG_DEBUG(...) ::MLog::Write(LogCore, ::ELogLevel::Debug, __VA_ARGS__)
#define LOG_INFO(...)  ::MLog::Write(LogCore, ::ELogLevel::Info, __VA_ARGS__)
#define LOG_WARN(...)  ::MLog::Write(LogCore, ::ELogLevel::Warn, __VA_ARGS__)
#define LOG_ERROR(...) ::MLog::Write(LogCore, ::ELogLevel::Error, __VA_ARGS__)
#define LOG_FATAL(...) ::MLog::Write(LogCore, ::ELogLevel::Critical, __VA_ARGS__)

#define LOG_FATAL_EX(Cat, Fmt, ...)            \
    do {                                       \
        va_list __ap;                          \
        va_start(__ap, Fmt);                   \
        ::MLog::HandleFatal((Cat), Fmt, __ap); \
        va_end(__ap);                          \
    } while (0)
