#pragma once

#include "Common/MLib.h"
#include "EventLoopStep.h"

/**
 * 主事件循环：容纳若干子 EventLoop（IEventLoopStep），每帧按注册顺序依次执行各子的 RunOnce(timeoutMs)。
 * 不持有子循环所有权，仅持有指针；子循环由外部（如 MNetServerBase）管理。
 */
class MEventLoop
{
public:
    /** 注册子循环；每帧 RunOnce 时对其调用 step->RunOnce(timeoutMs)。先注册先执行。 */
    void AddStep(IEventLoopStep* step, int timeoutMs = 0);

    /** 执行一帧：依次调用所有已注册 step 的 RunOnce(timeoutMs)。 */
    void RunOnce();

    /** 循环 RunOnce 直到 Stop()；通常由外部驱动单次 RunOnce，不直接调用 Run()。 */
    void Run();
    void Stop() { bRunning = false; }
    bool IsRunning() const { return bRunning; }

private:
    struct SStep
    {
        IEventLoopStep* Step = nullptr;
        int TimeoutMs = 0;
    };
    TVector<SStep> Steps;
    bool bRunning = false;
};
