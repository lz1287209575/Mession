#pragma once

/** 子事件循环/单步执行抽象：每帧由主循环调用 RunOnce(timeoutMs)。 */
class IEventLoopStep
{
public:
    virtual ~IEventLoopStep() = default;
    /** 执行一帧；timeoutMs 由子类解释（如网络 poll 超时、任务循环可忽略）。 */
    virtual void RunOnce(int timeoutMs = 0) = 0;
};
