#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Log/LogLevel.h"
#include "Common/Runtime/Log/LogSinks.h"

#include <mutex>

// 控制台 Sink：写入 stdout，可选按级别着色（首版不实现着色）
class MConsoleSink : public ILogSink
{
public:
    explicit MConsoleSink(ELogLevel InMinLevel = ELogLevel::Trace)
        : MinLevel(InMinLevel)
    {
    }
    void Write(ELogLevel Level, const MString& FormattedLine) override;
    void SetMinLevel(ELogLevel InLevel) { MinLevel = InLevel; }
    ELogLevel GetMinLevel() const override { return MinLevel; }

private:
    ELogLevel MinLevel;
    std::mutex WriteMutex;
};