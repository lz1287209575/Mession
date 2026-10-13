/**
 * @file SignalHandler.h
 * @brief MSignalHandler —— 进程级信号集中管理(SIGINT/SIGTERM 优雅退出,SIGPIPE 忽略)。
 */
#pragma once

#include "Common/Runtime/MLib.h"
#include <atomic>
#include <csignal>

/**
 * @brief MSignalHandler - 进程级信号集中管理。
 *
 * @detail 职责:
 *   - SIGINT / SIGTERM:置 bShutdownRequested 标志,由主循环轮询后优雅退出
 *   - SIGPIPE:忽略;对端断开后 write 只返回 EPIPE,不因此杀掉进程
 *
 * 用法(main 入口):
 *   MSignalHandler::Install();            // 注册 sigaction
 *   ... Run 主循环 ...
 *   while (bRunning) { if (MSignalHandler::IsShutdownRequested()) break; }
 *
 * 线程安全:信号处理函数里只能调用 async-signal-safe 的函数,因此只做
 * std::atomic<bool> 的 release 存储,不调用 printf / LOG_* / new。由此也
 * 不需要 self-pipe / eventfd。Install() 内部的 LOG_* 调用发生在普通上下文,
 * 不受该限制。
 */
class MSignalHandler {
    public:
    /** 注册 SIGINT/SIGTERM → 设置 bShutdownRequested；SIGPIPE → 忽略。*/
    static void Install();

    /** 复位标志位（用于测试 / 重入场景）。*/
    static void Reset() {
        sShutdownRequested.store(false, std::memory_order_release);
    }

    /** 查询是否有未处理的退出信号。线程安全。*/
    static bool IsShutdownRequested() {
        return sShutdownRequested.load(std::memory_order_acquire);
    }

    private:
    static std::atomic<bool> sShutdownRequested;
    static void              HandleSignal(int Signum);
};