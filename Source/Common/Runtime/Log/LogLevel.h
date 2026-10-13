#pragma once

/**
 * @file LogLevel.h
 * @brief 日志级别与驱逐 / 刷新策略枚举及互转函数。
 *
 * @detail 与设计规范保持一致:FATAL 语义映射为 ELogLevel::Critical。
 */
enum class ELogLevel : int { Trace = 0, Debug = 1, Info = 2, Warn = 3, Error = 4, Critical = 5 };

/**
 * @brief 缓冲区满时的驱逐策略。
 */
enum class EEvictionPolicy : uint8 {
    DropNewest  = 0, // 丢弃新到达的日志
    DropOldest  = 1, // 默认,ERROR+ 保护
    BlockOnFull = 2, // 阻塞等待（用于关键日志）
};

/**
 * @brief 驱逐原因 —— 用于指标统计与监控告警。
 */
enum class EEvictionReason : uint8 {
    DroppedOverflow   = 0, // 普通日志被丢弃
    ProtectedOverflow = 1, // 保护策略下被丢弃
    DiskFull          = 2, // 落盘失败（磁盘满）
    NetworkLost       = 3, // 网络丢失（远端 Sink）
};

/**
 * @brief 异步落盘的刷新策略。
 */
enum class EFlushPolicy : uint8 {
    Interval       = 0, // 10ms 定时
    SizeThreshold  = 1, // 64KB 阈值
    IntervalOrSize = 2, // 二者满足其一(默认)
};

inline const char* LogLevelToString(ELogLevel Level) {
    switch (Level) {
    case ELogLevel::Trace:
        return "TRACE";
    case ELogLevel::Debug:
        return "DEBUG";
    case ELogLevel::Info:
        return "INFO";
    case ELogLevel::Warn:
        return "WARN";
    case ELogLevel::Error:
        return "ERROR";
    case ELogLevel::Critical:
        return "FATAL";
    default:
        return "?";
    }
}

inline bool LogLevelFromInt(int Value, ELogLevel& OutLevel) {
    if (Value >= 0 && Value <= 5) {
        OutLevel = static_cast<ELogLevel>(Value);
        return true;
    }
    return false;
}

inline ELogLevel LegacyIntToLevel(int Value) {
    if (Value <= 0) {
        return ELogLevel::Debug;
    }
    if (Value >= 5) {
        return ELogLevel::Critical;
    }
    return static_cast<ELogLevel>(Value);
}
