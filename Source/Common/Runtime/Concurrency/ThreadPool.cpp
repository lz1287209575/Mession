#include "Common/Runtime/Concurrency/ThreadPool.h"

MThreadPool::MThreadPool(size_t NumThreads)
{
    if (NumThreads == 0)
    {
        NumThreads = 1;
    }
    Workers.reserve(NumThreads);
    for (size_t i = 0; i < NumThreads; ++i)
    {
        Workers.emplace_back([this]()
        {
            MTaskQueue::TTask Task;
            while (Queue.Pop(Task))
            {
                if (Task)
                {
                    try
                    {
                        Task();
                    }
                    catch (...)
                    {
                    }
                }
            }
        });
    }
}

bool MThreadPool::Submit(TTask Task)
{
    return Queue.Push(std::move(Task));
}

void MThreadPool::Shutdown()
{
    if (bShutdown)
    {
        return;
    }
    bShutdown = true;
    Queue.Shutdown();
    for (std::thread& W : Workers)
    {
        if (W.joinable())
        {
            W.join();
        }
    }
}
