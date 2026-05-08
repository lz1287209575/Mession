#pragma once

#include <functional>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <future>
#include <atomic>
#include <algorithm>

namespace MHeaderTool
{

// ============================================================================
// Thread Pool - 线程池
// ============================================================================

class ThreadPool
{
public:
    explicit ThreadPool(size_t numThreads)
    {
        if (numThreads == 0)
        {
            numThreads = std::thread::hardware_concurrency();
            if (numThreads == 0)
            {
                numThreads = 2;
            }
        }

        Workers_.reserve(numThreads);
        for (size_t i = 0; i < numThreads; ++i)
        {
            Workers_.emplace_back([this] { WorkerThread(); });
        }
    }

    ~ThreadPool()
    {
        Stop();
    }

    // 提交任务
    template<typename F>
    auto Submit(F&& task) -> std::future<std::invoke_result_t<F>>
    {
        using ResultType = std::invoke_result_t<F>;

        auto packagedTask = std::make_shared<std::packaged_task<ResultType()>>(std::forward<F>(task));
        auto future = packagedTask->get_future();

        {
            std::lock_guard<std::mutex> lock(Mutex_);
            Tasks_.push([packagedTask]() { (*packagedTask)(); });
        }

        CV_.notify_one();
        return future;
    }

    // 停止线程池
    void Stop()
    {
        {
            std::lock_guard<std::mutex> lock(Mutex_);
            bStopping_ = true;
        }
        CV_.notify_all();
        for (auto& worker : Workers_)
        {
            if (worker.joinable())
            {
                worker.join();
            }
        }
        Workers_.clear();
    }

    size_t GetNumThreads() const { return Workers_.size(); }

private:
    void WorkerThread()
    {
        while (true)
        {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(Mutex_);
                CV_.wait(lock, [this] { return bStopping_ || !Tasks_.empty(); });

                if (bStopping_ && Tasks_.empty())
                {
                    return;
                }

                if (!Tasks_.empty())
                {
                    task = std::move(Tasks_.front());
                    Tasks_.pop();
                }
            }

            if (task)
            {
                task();
            }
        }
    }

    std::vector<std::thread> Workers_;
    std::queue<std::function<void()>> Tasks_;
    std::mutex Mutex_;
    std::condition_variable CV_;
    std::atomic<bool> bStopping_{false};
};

// ============================================================================
// Parallel Executor - 并行执行器
// ============================================================================

class ParallelExecutor
{
public:
    explicit ParallelExecutor(size_t numThreads = 0)
        : ThreadPool_(numThreads)
    {
    }

    // 并行执行任务
    template<typename F, typename... Args>
    void Execute(size_t count, F&& func, Args&&... args)
    {
        std::vector<std::future<void>> futures;
        futures.reserve(count);

        for (size_t i = 0; i < count; ++i)
        {
            futures.push_back(ThreadPool_.Submit(
                [func, i, args...]() { func(i, std::forward<Args>(args)...); }));
        }

        // 等待所有任务完成
        for (auto& future : futures)
        {
            future.get();
        }
    }

    // 并行处理并收集结果
    template<typename F, typename R, typename... Args>
    std::vector<R> ParallelMap(size_t count, F&& func, Args&&... args)
    {
        std::vector<std::future<R>> futures;
        futures.reserve(count);

        for (size_t i = 0; i < count; ++i)
        {
            futures.push_back(ThreadPool_.Submit(
                [func, i, args...]() { return func(i, std::forward<Args>(args)...); }));
        }

        std::vector<R> results;
        results.reserve(count);
        for (auto& future : futures)
        {
            results.push_back(future.get());
        }

        return results;
    }

    // 并行处理 items
    template<typename ItemType, typename F>
    void ParallelForEach(std::vector<ItemType>& items, F&& func)
    {
        std::vector<std::future<void>> futures;
        futures.reserve(items.size());

        for (auto& item : items)
        {
            futures.push_back(ThreadPool_.Submit(
                [&item, &func]() { func(item); }));
        }

        for (auto& future : futures)
        {
            future.get();
        }
    }

    // 带进度报告的并行处理
    template<typename ItemType, typename F>
    void ParallelForEachWithProgress(
        std::vector<ItemType>& items,
        F&& func,
        std::function<void(size_t, size_t)> progressCallback)
    {
        std::atomic<size_t> completed{0};
        size_t total = items.size();

        std::vector<std::future<void>> futures;
        futures.reserve(items.size());

        for (auto& item : items)
        {
            futures.push_back(ThreadPool_.Submit(
                [&item, &func, &completed, total, &progressCallback]() {
                    func(item);
                    size_t done = ++completed;
                    if (progressCallback)
                    {
                        progressCallback(done, total);
                    }
                }));
        }

        for (auto& future : futures)
        {
            future.get();
        }
    }

    size_t GetNumThreads() const { return ThreadPool_.GetNumThreads(); }

private:
    ThreadPool ThreadPool_;
};

// ============================================================================
// Task Chunker - 任务分块器（用于负载均衡）
// ============================================================================

class TaskChunker
{
public:
    explicit TaskChunker(size_t totalItems, size_t numWorkers)
        : TotalItems_(totalItems)
        , NumWorkers_(numWorkers)
        , BaseChunkSize_(totalItems / numWorkers)
        , Remainder_(totalItems % numWorkers)
    {
    }

    // 获取第 i 个 worker 的任务范围
    std::pair<size_t, size_t> GetChunk(size_t workerIndex) const
    {
        if (workerIndex >= NumWorkers_)
        {
            return {0, 0};
        }

        size_t start = workerIndex * BaseChunkSize_ + std::min(workerIndex, Remainder_);
        size_t end = start + BaseChunkSize_ + (workerIndex < Remainder_ ? 1 : 0);

        return {start, end};
    }

    size_t TotalItems() const { return TotalItems_; }
    size_t NumWorkers() const { return NumWorkers_; }

private:
    size_t TotalItems_;
    size_t NumWorkers_;
    size_t BaseChunkSize_;
    size_t Remainder_;
};

}  // namespace MHeaderTool
