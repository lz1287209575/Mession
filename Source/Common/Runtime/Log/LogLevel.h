#pragma once


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

inline ELogLevel LegacyIntToLevel(int Value)
{
    if (Value <= 0)
    {
        return ELogLevel::Debug;
    }
    if (Value >= 5)
    {
        return ELogLevel::Critical;
    }
    return static_cast<ELogLevel>(Value);
}
