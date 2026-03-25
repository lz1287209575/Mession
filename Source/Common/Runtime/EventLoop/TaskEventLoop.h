#pragma once

#include "Common/Runtime/Concurrency/ITaskRunner.h"
#include "Common/Runtime/EventLoop/EventLoopStep.h"

/** 纯任务事件循环：仅维护任务队列，RunOnce 只执行已投递的任务；实现 ITaskRunner、IEventLoopStep。 */
class MTaskEventLoop : public ITaskRunner, public IEventLoopStep
{
public:
    void PostTask(TTask Task) override;

    void RunOnce(int timeoutMs = 0) override;

private:
    TDeque<TTask> PendingTasks;
    mutable std::mutex TaskMutex;
};
