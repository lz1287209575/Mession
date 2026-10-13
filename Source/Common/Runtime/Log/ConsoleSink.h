/**
 * @file ConsoleSink.h
 * @brief MConsoleSink —— 新管线(规范 §5.8)控制台 sink,向 stdout 写
 * 序列化文本行。
 *
 * @detail 低于 MinLevel 的记录在写线程被丢;每条被接受的记录写成一行:
 *
 *   [ISO-timestamp] [LEVEL] [CatName] [file:line] message\n
 *
 * bUseColor 预留给后续 ANSI 彩色透传,首版一律纯文本;作为公开字段
 * 暴露,调用方无需 setter 即可翻转。
 *
 * 对 stdout 而言 Open/Close 是空操作(libc 流始终可用),仍实现以
 * 满足 ILogSink 生命周期契约。
 */
#pragma once

#include "Common/Runtime/Log/LogLevel.h"
#include "Common/Runtime/Log/LogRecord.h"
#include "Common/Runtime/Log/LogSinks.h"
#include "Common/Runtime/MLib.h"
#include <mutex>


class MConsoleSink : public ILogSink {
    public:
    MConsoleSink() = default;

    // 新管线
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

    // 配置
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
