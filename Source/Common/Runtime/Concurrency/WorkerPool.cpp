/**
 * @file WorkerPool.cpp
 * @brief MWorkerPool 实现.
 */
#include "Common/Runtime/Concurrency/WorkerPool.h"

#include "Common/Runtime/Log/Log.h"

#include <chrono>
#include <cstdlib>
#include <exception>
#include <thread>

namespace mession::concurrency {
    void MWorkerPool::Start(uint32 InWorkerCount) {
        if (bRunning.load()) {
            LOG_WARN("MWorkerPool already started");
            return;
        }
        if (InWorkerCount == 0) {
            LOG_FATAL("MWorkerPool::Start called with 0 workers");
            std::abort();
        }

        WorkerCount = InWorkerCount;
        Workers.reserve(InWorkerCount);

        bRunning.store(true);
        for (uint32 Index = 0; Index < InWorkerCount; ++Index) {
            Workers.push_back(MakeShared<std::thread>([this] { WorkerLoop(); }));
        }

        LOG_INFO("MWorkerPool started with %u workers", static_cast<unsigned>(InWorkerCount));
    }

    void MWorkerPool::Stop() {
        if (!bRunning.load()) {
            return;
        }
        bRunning.store(false);
        // 唤醒所有在 wait_for 的 Worker,让它们看见 bRunning==false 后退出
        QueueCv.notify_all();

        for (auto& Worker : Workers) {
            if (Worker != nullptr && Worker->joinable()) {
                Worker->join();
            }
        }
        Workers.clear();
        WorkerCount = 0;

        LOG_INFO("MWorkerPool stopped");
    }

    MWorkerPool::~MWorkerPool() {
        Stop();
    }

    void MWorkerPool::Enqueue(TFunction<void()> InTask) {
        {
            std::lock_guard<std::mutex> Lock(QueueMutex);
            TaskQueue.push(std::move(InTask));
        }
        QueueCv.notify_one();
    }

    void MWorkerPool::WorkerLoop() {
        // 注意:1ms 超时是"近实时" + "不忙等"的折中
        // 真生产可以按 CPU 利用率调,或换无锁 MPSC queue(每 Sub 一个)
        constexpr auto PollInterval = std::chrono::milliseconds(1);

        while (true) {
            TFunction<void()> Task;
            {
                std::unique_lock<std::mutex> Lock(QueueMutex);
                QueueCv.wait_for(Lock, PollInterval, [this] { return !TaskQueue.empty() || !bRunning.load(); });

                if (!bRunning.load() && TaskQueue.empty()) {
                    return;
                }
                if (TaskQueue.empty()) {
                    continue;
                }
                Task = std::move(TaskQueue.front());
                TaskQueue.pop();
            }

            // 业务 lambda 抛异常不应杀掉 Worker
            try {
                Task();
            } catch (const std::exception& Exception) {
                LOG_ERROR("Worker task threw exception: %s", Exception.what());
            } catch (...) {
                LOG_ERROR("Worker task threw unknown exception");
            }
        }
    }
} // namespace mession::concurrency