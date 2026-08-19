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

// MLog — public facade (spec §5.9).
//
// Lifecycle: call Init() exactly once at startup with the desired SLogInitParams.
// At shutdown call Shutdown() to drain the queue and join all worker threads.
//
// Hot-path: Write() is the entry point for every log call site. It applies
// the per-category filter, resolves the routing decision (SinkMask), then
// enqueues a single SLogRecord into the lock-free MPSC ring buffer. The
// caller never blocks on disk or network — those happen on the dispatcher
// and writer threads.
//
// FATAL path: HandleFatal() bypasses the ring buffer entirely. It snapshots
// the recent records, calls MCoredumpSink, then raises SIGABRT. This is
// the only path that does anything synchronous from the caller's thread.
namespace MLog {
    // Startup. Wires up sinks, registry, dispatcher, writers. Idempotent:
    // a second call is a no-op.
    void Init(const SLogInitParams& Params);

    // Shutdown. Drains the ring buffer (with a 5s timeout) and joins all
    // worker threads. Safe to call multiple times.
    void Shutdown();

    // Hot-path entry. The Category pointer is non-owning; must be either
    // a project-defined SLogCategory* (LogCore / LogNet / etc.) or a
    // pointer returned by MLogRegistry::RegisterCategory.
    void Write(const SLogCategory* Category, ELogLevel Level, const char* Fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
        __attribute__((format(printf, 3, 4)))
#endif
        ;
    void WriteV(const SLogCategory* Category, ELogLevel Level, const char* Fmt, va_list Args);

    // FATAL synchronous path.
    void HandleFatal(const SLogCategory* Category, const char* Fmt, va_list Args);

    // Runtime configuration.
    void SetCategoryLevel(const SLogCategory& Category, ELogLevel Level);
    void SetCategorySuppressed(const SLogCategory& Category, bool bSuppressed);

    // Apply a parsed JSON config. Existing rules remain unless overridden
    // by name. Returns true if at least one entry was applied.
    bool ApplyCategoryConfig(const TVector<SLogCategoryConfig>& Configs);
    bool ApplyRouteConfig(const TVector<SLogRouteConfig>& Configs);

    // Drain — used by main before exit when Shutdown is too heavy.
    void Flush(int TimeoutMs = 1000);
} // namespace MLog

// Replaces the deleted MLogger::IsDebugBuild() (gated by NDEBUG).
inline constexpr bool MLogIsDebugBuild() {
#if defined(NDEBUG)
    return false;
#else
    return true;
#endif
}

// ---- Macros (spec §4.3) ----------------------------------------------------
//
// Project category macros expand to MLog::Write(LogXxx, ELogLevel::Level, ...).
// The 5 legacy LOG_* macros are preserved so existing code keeps compiling;
// they map to LogCore at the corresponding level.
//
// LOG_FATAL_EX(Cat, ...) is the synchronous coredump path.
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
