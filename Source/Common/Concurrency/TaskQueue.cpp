#include "TaskQueue.h"

MTaskQueue::MTaskQueue() = default;

bool MTaskQueue::Push(TTask Task)
{
    if (!Task)
    {
        return true;
    }
    std::lock_guard<std::mutex> Lock(Mutex);
    if (bShutdown)
    {
        return false;
    }
    Queue.push_back(std::move(Task));
    Cond.notify_one();
    return true;
}

bool MTaskQueue::TryPop(TTask& OutTask)
{
    std::lock_guard<std::mutex> Lock(Mutex);
    if (Queue.empty())
    {
        return false;
    }
    OutTask = std::move(Queue.front());
    Queue.pop_front();
    return true;
}

bool MTaskQueue::Pop(TTask& OutTask)
{
    std::unique_lock<std::mutex> Lock(Mutex);
    Cond.wait(Lock, [this]() { return bShutdown || !Queue.empty(); });
    if (Queue.empty())
    {
        return false;
    }
    OutTask = std::move(Queue.front());
    Queue.pop_front();
    return true;
}

bool MTaskQueue::Pop(TTask& OutTask, int TimeoutMs)
{
    std::unique_lock<std::mutex> Lock(Mutex);
    if (!Cond.wait_for(Lock, std::chrono::milliseconds(TimeoutMs),
        [this]() { return bShutdown || !Queue.empty(); }))
    {
        return false;
    }
    if (Queue.empty())
    {
        return false;
    }
    OutTask = std::move(Queue.front());
    Queue.pop_front();
    return true;
}

size_t MTaskQueue::Size() const
{
    std::lock_guard<std::mutex> Lock(Mutex);
    return Queue.size();
}

void MTaskQueue::Clear()
{
    std::lock_guard<std::mutex> Lock(Mutex);
    Queue.clear();
}

void MTaskQueue::Shutdown()
{
    std::lock_guard<std::mutex> Lock(Mutex);
    bShutdown = true;
    Cond.notify_all();
}

bool MTaskQueue::IsShutdown() const
{
    std::lock_guard<std::mutex> Lock(Mutex);
    return bShutdown;
}
