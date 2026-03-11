#pragma once

#include <string>
#include <iostream>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <mutex>
#include <cstdarg>

// 简单的日志系统
class MLogger
{
private:
    inline static std::mutex LogMutex;
    inline static int MinLevel = 1;
    inline static bool bConsoleOutput = true;
    
    MLogger() = default;

    static void VLog(int Level, const char* Format, va_list Args)
    {
        if (Level < MinLevel || !bConsoleOutput)
        {
            return;
        }

        char Buffer[4096];
        va_list ArgsCopy;
        va_copy(ArgsCopy, Args);
        vsnprintf(Buffer, sizeof(Buffer), Format, ArgsCopy);
        va_end(ArgsCopy);

        std::lock_guard<std::mutex> Lock(LogMutex);

        auto Now = std::chrono::system_clock::now();
        auto TimeT = std::chrono::system_clock::to_time_t(Now);

        const char* LevelStr[] = {"DEBUG", "INFO", "WARN", "ERROR", "FATAL"};

#if defined(_MSC_VER) || (defined(__MINGW32__) && !defined(__USE_MINGW_ANSI_STDIO))
        std::tm Tm = {};
        if (localtime_s(&Tm, &TimeT) == 0)
        {
            std::cout << std::put_time(&Tm, "%Y-%m-%d %H:%M:%S");
        }
        else
        {
            std::cout << "????-??-?? ??:??:??";
        }
#else
        std::cout << std::put_time(std::localtime(&TimeT), "%Y-%m-%d %H:%M:%S");
#endif
        std::cout << " [" << LevelStr[Level] << "] " << Buffer << std::endl;
    }
    
public:
    static void Init(const std::string& /*LogFileName*/ = "", int InMinLevel = 1)
    {
        MinLevel = InMinLevel;
    }
    
    static void SetConsoleOutput(bool bEnable) { bConsoleOutput = bEnable; }
    
    // 带格式的日志 - 唯一的日志方法
    static void Log(int Level, const char* Format, ...)
    {
        va_list Args;
        va_start(Args, Format);
        VLog(Level, Format, Args);
        va_end(Args);
    }
    
    // 便捷方法 - 只有一个Log方法，带格式
    static void Debug(const char* fmt, ...)   { va_list ap; va_start(ap, fmt); VLog(0, fmt, ap); va_end(ap); }
    static void Info(const char* fmt, ...)    { va_list ap; va_start(ap, fmt); VLog(1, fmt, ap); va_end(ap); }
    static void Warning(const char* fmt, ...) { va_list ap; va_start(ap, fmt); VLog(2, fmt, ap); va_end(ap); }
    static void Error(const char* fmt, ...)   { va_list ap; va_start(ap, fmt); VLog(3, fmt, ap); va_end(ap); }
    static void Fatal(const char* fmt, ...)   { va_list ap; va_start(ap, fmt); VLog(4, fmt, ap); va_end(ap); }
};

// 日志宏
#define LOG_DEBUG(...)   MLogger::Debug(__VA_ARGS__)
#define LOG_INFO(...)    MLogger::Info(__VA_ARGS__)
#define LOG_WARN(...)    MLogger::Warning(__VA_ARGS__)
#define LOG_ERROR(...)   MLogger::Error(__VA_ARGS__)
#define LOG_FATAL(...)   MLogger::Fatal(__VA_ARGS__)
