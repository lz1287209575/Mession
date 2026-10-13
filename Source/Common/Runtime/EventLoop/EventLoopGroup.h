/**
 * @file EventLoopGroup.h
 * @brief MEventLoopGroup —— 按注册顺序逐帧编排多个子事件循环的主循环容器。
 *
 * @detail 本类不是循环实现(不继承 IEventLoop),只做编排:每帧依次对每个
 *   已注册的 IEventLoop 调用一次 RunOnce(storedTimeoutMs)。它不持有子循环
 *   所有权(仅保存裸指针),子循环由外部持有者(如 MNetServerBase)负责
 *   生命周期与线程归属。非线程安全:AddStep/RunOnce/Run 必须在同一线程调用。
 */
#pragma once

#include "Common/Runtime/EventLoop/IEventLoop.h"
#include "Common/Runtime/MLib.h"

/**
 * @brief MEventLoopGroup - 子事件循环集合 + 单步调度。
 *
 * @detail 典型用法:
 *   1. AddStep(&SubLoop, TimeoutMs) —— 注册,先注册先执行
 *   2. RunOnce() —— 由外部主循环每帧调用一次
 *   3. Stop() —— 仅 Run() 内部循环会观察该标志
 */
class MEventLoopGroup {
    public:
    /**
     * @brief AddStep - 注册一个子循环及其单步超时。
     * @param step 子循环指针;为 nullptr 时静默忽略(不注册、不报错)
     * @param timeoutMs 该子循环每帧 RunOnce 时使用的超时(毫秒),原样保存不做校验
     */
    void AddStep(IEventLoop* step, int timeoutMs = 0);

    /** @brief RunOnce - 执行一帧:按注册顺序依次调用所有子循环的 RunOnce(各自超时)。 */
    void RunOnce();

    /**
     * @brief Run - 循环调用 RunOnce() 直到 Stop()。
     *
     * @detail 注意:子循环超时全为 0 时该循环会空转吃满 CPU,正常路径应由
     *   外部主循环单次调用 RunOnce(),不要依赖 Run() 来驱动。
     */
    void Run();
    /** @brief Stop - 请求退出 Run() 循环;运行中也可从子循环回调内调用。 */
    void Stop() {
        bRunning = false;
    }
    /** @brief IsRunning - 当前是否处于 Run() 循环中。 */
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
