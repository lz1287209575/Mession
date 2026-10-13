/**
 * @file LogSinks.h
 * @brief ILogSink 新管线 sink 契约(规范 §5.8),以及 ELogSinkId 位定义。
 *
 * @detail 生命周期 / 每批:
 *   Open()        — 准备 sink(打开文件、分配缓冲等)。默认返回 true,
 *                   无需准备的 sink(如内存环)可不必重写。
 *   Close()       — 释放资源,默认空操作。
 *   WriteBatch()  — 消费一批 SLogRecord 加调用方提供的暂存 OutBuffer,
 *                   序列化进 OutBuffer 后发往底层流。每个新管线 sink
 *                   必须实现自己的序列化。仅走旧 MLogger 路径的 sink
 *                   (现有 MConsoleSink / MFileSink)改为重写旧接口
 *                   Write(),继承这里的空实现即可。
 *   Flush()       — 把待写字节推给 OS,默认空操作。
 *
 * 单条级别门控 / 自省:
 *   MinLevel()    — 低于该级别的记录在写线程被丢(默认 Trace 即全收)。
 *   Name()        — 路由 / 指标打标用的短标识,默认返回 ""。
 *
 * 旧桥接(为兼容仍存在的 MLogger 保留——见 ConsoleLogSink.h 与
 * FileLogSink.h,二者继承 ILogSink 并重写 Write() 接入现有 MLogger 管线):
 *   Write(ELogLevel, const MString&) — 默认空操作。
 *   GetMinLevel()                    — 默认返回 MinLevel()。
 *
 * 这些旧方法带非纯默认实现,使现有 MConsoleSink / MFileSink(仍被
 * MLogger 使用)保持具体类。待 MLogger 自身迁到新管线后移除
 * (规范 §12 第 5 步)。
 */
#pragma once

#include "Common/Runtime/Log/LogLevel.h"
#include "Common/Runtime/Log/LogRecord.h"
#include "Common/Runtime/MLib.h"


class ILogSink {
    public:
    virtual ~ILogSink() = default;

    // --- 新管线(规范 §5.8) ---
    virtual bool Open() {
        return true;
    }
    virtual void Close() {
    }
    // WriteBatch 有意设为非纯虚:走 MLogger 路径的旧 sink
    // (ConsoleLogSink / FileLogSink)无需实现新契约也能工作。
    // 新管线 sink(ConsoleSink / RollingFileSink)重写它。
    //
    // 关于缓冲区类型:需求片段写的是 `TSpan<char>`,但 TSpan 是本项目
    // 的只读视图别名(现由兼容 cpp17 的 MSpan<const T> 支撑),会把缓冲
    // 区变成只读。语义上"sink 往里填序列化文本再写到底层流"要求可变
    // 暂存,故这里用 TSpanMutable<char>(= MSpan<char>)。存储仍由调用方
    // 提供。
    virtual void WriteBatch(TSpan<const SLogRecord> /*Batch*/, TSpanMutable<char> /*OutBuffer*/) {
    }
    virtual void Flush() {
    }
    virtual ELogLevel MinLevel() const {
        return ELogLevel::Trace;
    }
    virtual const char* Name() const {
        return "";
    }

    // --- 为仍存在的 MLogger 路径保留的旧桥接 ---
    virtual void Write(ELogLevel /*Level*/, const MString& /*FormattedLine*/) {
    }
    virtual ELogLevel GetMinLevel() const {
        return MinLevel();
    }
};

// =============================================================================
// ELogSinkId —— SLogRecord::SinkMask / SLogRouteRule 32 位路由表中的
// 位序号。自 LogRouter.h(Task 4)上移到此,让枚举与消费它的接口相邻。
// 位值 0..4 属稳定 ABI,禁止重排。
// =============================================================================
enum class ELogSinkId : uint32 {
    Console  = 0, // 1u << 0
    File     = 1, // 1u << 1
    Udp      = 2, // 1u << 2
    Tcp      = 3, // 1u << 3
    Coredump = 4, // 1u << 4
};

inline uint32 MakeSinkMask(ELogSinkId Id) {
    return 1u << static_cast<uint32>(Id);
}
