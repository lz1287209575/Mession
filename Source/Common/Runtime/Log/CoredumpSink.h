#pragma once
#include "Common/Runtime/Log/LogLevel.h"
#include "Common/Runtime/Log/LogRecord.h"
#include "Common/Runtime/Log/LogSinks.h"
#include "Common/Runtime/MLib.h"

#include <cstdarg>
#include <functional>

/**
 * @file CoredumpSink.h
 * @brief MCoredumpSink —— 同步 FATAL 现场捕获(规范 §6.1)。
 *
 * @detail 由 MLog::HandleFatal 触发:绕过异步环形队列,扫描队列尾部
 * (最近 N 条),收集进程与系统信息,把 JSON Lines dump 写到
 * Logs/coredump/<service>-<ts>.jsonl,随后抛 SIGABRT 让 OS 也生成 core。
 *
 * 我们故意不依赖应用栈调 backtrace() —— HandleFatal 运行时栈可能已
 * 部分展开。完整栈由 OS coredump 捕获;本类的职责是捕获日志尾部与
 * 进程上下文,而不是自己走栈。
 *
 * SConfig::UploadHook 预留给规范 §6.2。设置后 OnDumpComplete 会在
 * detached std::thread 上触发钩子,主路径保持同步。上传失败只把错误
 * 写 stderr。
 */
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