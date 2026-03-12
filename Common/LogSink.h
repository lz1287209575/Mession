#pragma once

#include "Core/NetCore.h"
#include <mutex>

// 日志级别（与设计规范一致，FATAL 映射为 Critical）
enum class ELogLevel : int
{
    Trace = 0,
    Debug = 1,
    Info  = 2,
    Warn  = 3,
    Error = 4,
    Critical = 5
};

inline const char* LogLevelToString(ELogLevel Level)
{
    switch (Level)
    {
        case ELogLevel::Trace:    return "TRACE";
        case ELogLevel::Debug:   return "DEBUG";
        case ELogLevel::Info:    return "INFO";
        case ELogLevel::Warn:    return "WARN";
        case ELogLevel::Error:   return "ERROR";
        case ELogLevel::Critical: return "FATAL";
        default:                 return "?";
    }
}

inline bool LogLevelFromInt(int Value, ELogLevel& OutLevel)
{
    if (Value >= 0 && Value <= 5)
    {
        OutLevel = static_cast<ELogLevel>(Value);
        return true;
    }
    return false;
}

// Sink 抽象：每条格式化后的日志一行，写入目标
class ILogSink
{
public:
    virtual ~ILogSink() = default;
    virtual void Write(ELogLevel Level, const FString& FormattedLine) = 0;
    virtual void Flush() {}
    // 低于此级别的日志不写入本 Sink
    virtual ELogLevel GetMinLevel() const { return ELogLevel::Trace; }
};

// 控制台 Sink：写入 stdout，可选按级别着色（首版不实现着色）
class MConsoleSink : public ILogSink
{
public:
    explicit MConsoleSink(ELogLevel InMinLevel = ELogLevel::Trace)
        : MinLevel(InMinLevel)
    {
    }
    void Write(ELogLevel Level, const FString& FormattedLine) override;
    void SetMinLevel(ELogLevel InLevel) { MinLevel = InLevel; }
    ELogLevel GetMinLevel() const override { return MinLevel; }

private:
    ELogLevel MinLevel;
    std::mutex WriteMutex;
};

// 文件 Sink：追加写入单文件
class MFileSink : public ILogSink
{
public:
    MFileSink() = default;
    ~MFileSink() override;
    bool Open(const FString& FilePath);
    void Close();
    void Write(ELogLevel Level, const FString& FormattedLine) override;
    void Flush() override;
    void SetMinLevel(ELogLevel InLevel) { MinLevel = InLevel; }
    ELogLevel GetMinLevel() const override { return MinLevel; }
    bool IsOpen() const { return Stream.is_open(); }

private:
    TOfstream Stream;
    ELogLevel MinLevel = ELogLevel::Trace;
    std::mutex WriteMutex;
};
