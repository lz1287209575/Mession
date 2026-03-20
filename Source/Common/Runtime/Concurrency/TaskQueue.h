#pragma once

#include "Common/Runtime/MLib.h"
#include <condition_variable>
#include <mutex>

/** 线程安全任务队列，多生产者多消费者；Shutdown 后 Push 返回 false，Pop 在空时立即返回。 */
class MTaskQueue
{
public:
    using TTask = TFunction<void()>;

    MTaskQueue();
    ~MTaskQueue() { Shutdown(); }

    bool Push(TTask Task);

    bool TryPop(TTask& OutTask);

    bool Pop(TTask& OutTask);
    bool Pop(TTask& OutTask, int TimeoutMs);

    size_t Size() const;
    void Clear();
    void Shutdown();
    bool IsShutdown() const;

private:
    TDeque<TTask> Queue;
    mutable std::mutex Mutex;
    std::condition_variable Cond;
    bool bShutdown = false;
};
