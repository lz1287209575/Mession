#include "TaskEventLoop.h"

void MTaskEventLoop::PostTask(TTask Task)
{
    if (!Task)
    {
        return;
    }
    std::lock_guard<std::mutex> Lock(TaskMutex);
    PendingTasks.push_back(std::move(Task));
}

void MTaskEventLoop::RunOnce(int /* timeoutMs */)
{
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
