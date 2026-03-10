#pragma once

#include <string>
#include <iostream>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <mutex>
#include <cstdarg>

// 简单的日志系统
class FLogger
{
private:
    inline static std::mutex LogMutex;
    inline static int MinLevel = 1;
    inline static bool bConsoleOutput = true;
    
    FLogger() = default;
    
public:
    static void Init(const std::string& LogFileName = "", int InMinLevel = 1)
    {
        MinLevel = InMinLevel;
    }
    
    static void SetConsoleOutput(bool bEnable) { bConsoleOutput = bEnable; }
    
    // 带格式的日志 - 唯一的日志方法
    static void Log(int Level, const char* Format, ...)
    {
        if (Level < MinLevel || !bConsoleOutput)
            return;
            
        char Buffer[4096];
        va_list Args;
        va_start(Args, Format);
        vsnprintf(Buffer, sizeof(Buffer), Format, Args);
        va_end(Args);
        
        std::lock_guard<std::mutex> Lock(LogMutex);
        
        // 时间
        auto Now = std::chrono::system_clock::now();
        auto TimeT = std::chrono::system_clock::to_time_t(Now);
        
        const char* LevelStr[] = {"DEBUG", "INFO", "WARN", "ERROR", "FATAL"};
        
        std::cout << std::put_time(std::localtime(&TimeT), "%Y-%m-%d %H:%M:%S");
        std::cout << " [" << LevelStr[Level] << "] " << Buffer << std::endl;
    }
    
    // 便捷方法 - 只有一个Log方法，带格式
    static void Debug(const char* fmt, ...)   { va_list ap; va_start(ap, fmt); Log(0, fmt, ap); va_end(ap); }
    static void Info(const char* fmt, ...)    { va_list ap; va_start(ap, fmt); Log(1, fmt, ap); va_end(ap); }
    static void Warning(const char* fmt, ...)  { va_list ap; va_start(ap, fmt); Log(2, fmt, ap); va_end(ap); }
    static void Error(const char* fmt, ...)   { va_list ap; va_start(ap, fmt); Log(3, fmt, ap); va_end(ap); }
    static void Fatal(const char* fmt, ...)   { va_list ap; va_start(ap, fmt); Log(4, fmt, ap); va_end(ap); }
};

// 日志宏
#define LOG_DEBUG(...)   FLogger::Debug(__VA_ARGS__)
#define LOG_INFO(...)    FLogger::Info(__VA_ARGS__)
#define LOG_WARN(...)    FLogger::Warning(__VA_ARGS__)
#define LOG_ERROR(...)   FLogger::Error(__VA_ARGS__)
#define LOG_FATAL(...)   FLogger::Fatal(__VA_ARGS__)
