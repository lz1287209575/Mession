#pragma once

#include "Common/MLib.h"
#include "TaskQueue.h"
#include <thread>

/** 固定数量工作线程 + 一个任务队列；Submit 入队，Shutdown 关队列并 join 所有线程。 */
class MThreadPool
{
public:
    using TTask = MTaskQueue::TTask;

    explicit MThreadPool(size_t NumThreads);
    ~MThreadPool() { Shutdown(); }

    MThreadPool(const MThreadPool&) = delete;
    MThreadPool& operator=(const MThreadPool&) = delete;

    bool Submit(TTask Task);

    void Shutdown();
    size_t GetNumThreads() const { return Workers.size(); }

private:
    MTaskQueue Queue;
    TVector<std::thread> Workers;
    bool bShutdown = false;
};
