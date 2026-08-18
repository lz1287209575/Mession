/**
 * @file WorkerPool.h
 * @brief MWorkerPool - 业务处理线程池,从共享 WorkQueue 取任务分发执行。
 */
#pragma once

#include "Common/Runtime/MLib.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace mession::concurrency {
    /**
     * @brief MWorkerPool - 业务处理 Worker 线程池.
     *
     * 任务队列是 MPMC 多生产者多消费者,所有 Worker 共享同一队列。
     * Worker 跑业务逻辑,处理完后通过 SubPool->Post 把响应送回对应 Sub。
     *
     * 与 Sub reactor 解耦:WorkerPool 不感知 Sub,只负责"业务计算"。
     *
     * 生命周期:
     *   1. Start(N) - 启动 N 个 Worker 线程
     *   2. Enqueue(Task) - 把任务投到 WorkQueue
     *   3. Stop() - 通知所有 Worker 退出 + join
     */
    class MWorkerPool {
        public:
        MWorkerPool() = default;
        ~MWorkerPool();

        MWorkerPool(const MWorkerPool&)            = delete;
        MWorkerPool& operator=(const MWorkerPool&) = delete;

        /**
         * @brief Start - 启动 N 个 Worker 线程.
         * @param InWorkerCount  Worker 数量,通常 CPU*2(业务 CPU-bound)
         */
        void Start(uint32 InWorkerCount);

        /**
         * @brief Stop - 通知所有 Worker 退出 + join.
         *
         * 重复调用安全。
         */
        void Stop();

        /**
         * @brief Enqueue - 把任务投到 WorkQueue,任意 Worker 取出执行.
         *
         * @param InTask 任务(在 Worker 线程执行,不在 caller 线程)
         */
        void Enqueue(TFunction<void()> InTask);

        /** @return 当前 Worker 数量. */
        uint32 GetWorkerCount() const {
            return WorkerCount;
        }

        private:
        /**
         * @brief WorkerLoop - 单 Worker 线程主循环.
         *
         * 用 condition_variable.wait_for 短超时(1ms)实现"近实时 + 不忙等"。
         * 业务 lambda 抛异常不应杀掉 Worker,所以每任务都包 try/catch。
         */
        void WorkerLoop();

        TQueue<TFunction<void()>>        TaskQueue;
        std::mutex                       QueueMutex;
        std::condition_variable          QueueCv;
        TVector<TSharedPtr<std::thread>> Workers;
        uint32                           WorkerCount = 0;
        std::atomic<bool>                bRunning{false};
    };
} // namespace mession::concurrency