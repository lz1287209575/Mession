#include "Common/Runtime/EventLoop/TaskEventLoop.h"

void MTaskEventLoop::PostTask(TTask Task)
{
    if (!Task)
    {
        return;
    }
    std::lock_guard<std::mutex> Lock(TaskMutex);
    PendingTasks.push_back(std::move(Task));
}

bool MTaskEventLoop::IsCurrentThread() const
{
    return std::this_thread::get_id() == OwnerThreadId.load(std::memory_order_acquire);
}

void MTaskEventLoop::RunOnce(int /* timeoutMs */)
{
    // Establish OwnerThreadId on first (and every) entry; same thread in practice,
    // so this is effectively a one-shot capture.
    OwnerThreadId.store(std::this_thread::get_id(), std::memory_order_release);

    TDeque<TTask> ToRun;
    {
        std::lock_guard<std::mutex> Lock(TaskMutex);
        ToRun.swap(PendingTasks);
    }
    for (TTask& T : ToRun)
    {
        if (T)
        {
            T();
        }
    }
}
