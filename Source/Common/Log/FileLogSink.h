#pragma once

#include "Common/MLib.h"
#include "LogLevel.h"
#include "LogSinks.h"

#include <mutex>

// 文件 Sink：追加写入单文件
class MFileSink : public ILogSink
{
public:
    MFileSink() = default;
    ~MFileSink() override;
    bool Open(const MString& FilePath);
    void Close();
    void Write(ELogLevel Level, const MString& FormattedLine) override;
    void Flush() override;
    void SetMinLevel(ELogLevel InLevel) { MinLevel = InLevel; }
    ELogLevel GetMinLevel() const override { return MinLevel; }
    bool IsOpen() const { return Stream.is_open(); }

private:
    TOfstream Stream;
    ELogLevel MinLevel = ELogLevel::Trace;
    std::mutex WriteMutex;
};
