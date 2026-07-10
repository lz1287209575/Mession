#pragma once

#include "Common/Runtime/MLib.h"
#include <atomic>
#include <csignal>

/**
 * MSignalHandler — 进程级信号集中管理。
 *
 * 用途：
 *   - SIGINT / SIGTERM：触发 Service 优雅退出（设置 bShutdownRequested 标志位）
 *   - SIGPIPE：默认忽略（write 失败返回 EPIPE，不杀进程）
 *
 * 用法（main 入口）：
 *   MSignalHandler::Install();            // 注册 sigaction
 *   ... Run 主循环 ...
 *   while (bRunning) {
 *       if (MSignalHandler::IsShutdownRequested()) break;
 *   }
 *
 * 线程安全：
 *   - 信号处理函数中只能调用 async-signal-safe 函数
 *   - 标志位使用 std::atomic<bool>，编译器/CPU 保证原子读
 *   - 不需要 self-pipe / eventfd
 */
class MSignalHandler
{
public:
    /** 注册 SIGINT/SIGTERM → 设置 bShutdownRequested；SIGPIPE → 忽略。*/
    static void Install();

    /** 复位标志位（用于测试 / 重入场景）。*/
    static void Reset() { sShutdownRequested.store(false, std::memory_order_release); }

    /** 查询是否有未处理的退出信号。线程安全。*/
    static bool IsShutdownRequested()
    {
        return sShutdownRequested.load(std::memory_order_acquire);
    }

private:
    static std::atomic<bool> sShutdownRequested;
    static void HandleSignal(int Signum);
};