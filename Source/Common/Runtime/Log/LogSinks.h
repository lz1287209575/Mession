#pragma once

#include "Common/Runtime/MLib.h"

#include "Common/Runtime/Log/LogLevel.h"

// Sink 抽象：每条格式化后的日志一行，写入目标
class ILogSink
{
public:
    virtual ~ILogSink() = default;
    virtual void Write(ELogLevel Level, const MString& FormattedLine) = 0;
    virtual void Flush() {}
    // 低于此级别的日志不写入本 Sink
    virtual ELogLevel GetMinLevel() const { return ELogLevel::Trace; }
};