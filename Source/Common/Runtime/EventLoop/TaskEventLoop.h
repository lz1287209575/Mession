#pragma once

#include "Common/Runtime/Async/IExecutor.h"
#include "Common/Runtime/EventLoop/IEventLoop.h"
#include "Common/Runtime/MLib.h"

/** 纯任务事件循环：仅维护任务队列，RunOnce 只执行已投递的任务；实现 IExecutor、IEventLoop。 */
class MTaskEventLoop : public mession::async::IExecutor, public IEventLoop {
    public:
    void Post(TTask Task) override;

    bool IsCurrentThread() const override;

    void RunOnce(int timeoutMs = 0) override;

    private:
    TDeque<TTask>      PendingTasks;
    mutable std::mutex TaskMutex;
    TAtomic<MThreadId> OwnerThreadId{}; // 默认空 id；RunOnce 首次进入时固化
};