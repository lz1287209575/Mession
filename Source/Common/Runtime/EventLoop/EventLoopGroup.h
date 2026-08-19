#pragma once

#include "Common/Runtime/EventLoop/IEventLoop.h"
#include "Common/Runtime/MLib.h"

/**
 * 主循环容器：容纳若干子循环（IEventLoop），每帧按注册顺序依次执行各子的 RunOnce(timeoutMs)。
 * 本身不是循环实现（不是 IEventLoop）；仅编排子循环，不持有所有权，仅持有指针；
 * 子循环由外部（如 MNetServerBase）管理。
 */
class MEventLoopGroup {
    public:
    /** 注册子循环；每帧 RunOnce 时对其调用 step->RunOnce(timeoutMs)。先注册先执行。 */
    void AddStep(IEventLoop* step, int timeoutMs = 0);

    /** 执行一帧：依次调用所有已注册 step 的 RunOnce(timeoutMs)。 */
    void RunOnce();

    /** 循环 RunOnce 直到 Stop()；通常由外部驱动单次 RunOnce，不直接调用 Run()。 */
    void Run();
    void Stop() {
        bRunning = false;
    }
    bool IsRunning() const {
        return bRunning;
    }

    private:
    struct SStep {
        IEventLoop* Step      = nullptr;
        int         TimeoutMs = 0;
    };
    TVector<SStep> Steps;
    bool           bRunning = false;
};

inline void MEventLoopGroup::AddStep(IEventLoop* Step, int TimeoutMs) {
    if (!Step) {
        return;
    }

    Steps.push_back(SStep{Step, TimeoutMs});
}

inline void MEventLoopGroup::RunOnce() {
    for (const SStep& Step : Steps) {
        if (Step.Step) {
            Step.Step->RunOnce(Step.TimeoutMs);
        }
    }
}

inline void MEventLoopGroup::Run() {
    bRunning = true;
    while (bRunning) {
        RunOnce();
    }
}
