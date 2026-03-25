#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Log/LogLevel.h"
#include "Common/Runtime/Log/LogSinks.h"

// 命名 Logger：支持多 Sink、可配置 pattern、源码位置
// Pattern 占位符：%t=时间 %l=级别 %n=logger名 %s=文件 %#=行号 %f=函数 %v=消息
// 默认 pattern: "[%t] [%l] [%n] %v"；需源码位置时可设为 "[%t] [%l] [%n] [%s:%#] %v"
class MLogger
{
public:
    explicit MLogger(MString Name);

    void SetLevel(ELogLevel InLevel) { Level = InLevel; }
    ELogLevel GetLevel() const { return Level; }
    const MString& GetName() const { return Name; }

    void SetPattern(MString InPattern) { Pattern = std::move(InPattern); }
    const MString& GetPattern() const { return Pattern; }

    void AddSink(ILogSink* Sink);
    void ClearSinks();

    // 核心接口：带源码位置的格式化日志
    void Log(ELogLevel Level, const char* File, int Line, const char* Func, const char* Fmt, ...);

    // 默认 logger（进程内单例），Init 时配置
    static MLogger* DefaultLogger();
    static void Init(const MString& LogFileName = "", int InMinLevel = 1);
    static void SetMinLevel(int InMinLevel);
    static void SetConsoleOutput(bool bEnable);

    // 命名 logger 注册表
    static MLogger* GetLogger(const MString& Name);
    static MLogger* GetOrCreateLogger(const MString& Name);

    // 启动横幅与完成行（使用默认 logger）
    static void LogStartupBanner(const char* ServiceName, uint16 Port, intptr_t Fd = -1);
    static void LogStarted(const char* ServiceName, double ElapsedSeconds);

private:
    void VLog(ELogLevel InLevel, const char* File, int Line, const char* Func, const char* Fmt, va_list Args);
    MString FormatLine(ELogLevel Level, const char* File, int Line, const char* Func, const char* Message) const;

    MString Name;
    MString Pattern;
    ELogLevel Level = ELogLevel::Info;
    TVector<ILogSink*> Sinks;
    std::mutex Mutex;
};

// 宏：带 __FILE__ / __LINE__ / __func__，使用默认 logger
#define LOG_DEBUG(...)   MLogger::DefaultLogger()->Log(ELogLevel::Debug,   __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_INFO(...)    MLogger::DefaultLogger()->Log(ELogLevel::Info,    __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_WARN(...)    MLogger::DefaultLogger()->Log(ELogLevel::Warn,    __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_ERROR(...)   MLogger::DefaultLogger()->Log(ELogLevel::Error,    __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_FATAL(...)   MLogger::DefaultLogger()->Log(ELogLevel::Critical, __FILE__, __LINE__, __func__, __VA_ARGS__)