#include "Common/Runtime/Concurrency/SignalHandler.h"
#include "Common/Runtime/Log/Logger.h"

#include <signal.h>

std::atomic<bool> MSignalHandler::sShutdownRequested{false};

void MSignalHandler::HandleSignal(int /*Signum*/)
{
    // async-signal-safe：仅原子写 + 简单赋值。
    // 禁止调用 printf / LOG_* / new —— 都不是 async-signal-safe。
    sShutdownRequested.store(true, std::memory_order_release);
}

void MSignalHandler::Install()
{
    struct sigaction Action;
    Action.sa_handler = &MSignalHandler::HandleSignal;
    sigemptyset(&Action.sa_mask);
    // SA_RESTART：被信号中断的系统调用自动重启（如 accept / read / write）。
    Action.sa_flags = SA_RESTART;

    if (sigaction(SIGINT, &Action, nullptr) != 0)
    {
        LOG_ERROR("MSignalHandler: sigaction(SIGINT) failed");
    }
    if (sigaction(SIGTERM, &Action, nullptr) != 0)
    {
        LOG_ERROR("MSignalHandler: sigaction(SIGTERM) failed");
    }

    // SIGPIPE：客户端断开后 write 失败不应杀进程；返回 EPIPE 让上层处理。
    struct sigaction Ignore;
    Ignore.sa_handler = SIG_IGN;
    sigemptyset(&Ignore.sa_mask);
    Ignore.sa_flags = 0;
    if (sigaction(SIGPIPE, &Ignore, nullptr) != 0)
    {
        LOG_ERROR("MSignalHandler: sigaction(SIGPIPE) failed");
    }

    LOG_INFO("MSignalHandler: installed (SIGINT/SIGTERM → shutdown, SIGPIPE → ignored)");
}