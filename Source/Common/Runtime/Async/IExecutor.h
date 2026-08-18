/**
 * @file IExecutor.h
 * @brief IExecutor - 通用执行器接口,用于"我应该在哪个执行上下文跑续体"的抽象.
 *
 * IExecutor 是 mession-async 库对外的核心接口。语义上等同 .NET 的
 * SynchronizationContext / Rust tokio 的 Handle:
 * - Post(Continuation):把续体投递到此 executor 绑定的执行线程/TaskLoop
 * - IsCurrentThread():检查当前调用方线程是否就是本 executor 的执行线程
 *
 * ## 与 ITaskRunner 的关系
 *
 * 历史上 MAsync 用的是 ITaskRunner(由 mession-core 库的 EventLoop 体系实现)。
 * 抽 MAsync 成独立库时,把 ITaskRunner 重新命名为 IExecutor 并搬到这里,
 * 让 mession-async 不再依赖 mession-core。
 *
 * 业务侧适配:
 * - MTaskEventLoop 实现 IExecutor(PostTask 已存在,改名即可)
 * - MNetEventLoop 不做 IExecutor:它只负责 poll 网络 fd,不含任务队列;
 *   异步续体一律投递到与其配对的 MTaskEventLoop(单 Reactor 下是
 *   MNetServerBase::TaskLoop,多 Reactor 下是 SubReactorPool 的 SubTaskLoops)
 *
 * ## 抽库边界
 *
 * mession-async 库(本文件)只依赖:
 * - MLib.h(基础类型)
 * - std::function
 *
 * 不依赖 mession-core / mession-reactor / mession-net。
 */
#pragma once

#include "Common/Runtime/MLib.h"

namespace mession::async {
    /**
     * @brief IExecutor - 通用执行器接口(取代 mession-core 的 ITaskRunner).
     *
     * 任何能"在某个执行线程上跑任务"的对象都可以实现 IExecutor:
     * - EventLoop 体系的 TaskLoop
     * - GUI 主线程队列
     * - Worker 线程池的某个 worker
     * - 当前线程直接跑(测试场景)
     */
    class IExecutor {
        public:
        using TTask = TFunction<void()>;

        virtual ~IExecutor() = default;

        /**
         * @brief Post - 投递一个任务到此 executor 绑定的执行线.
         * May be called from any thread(包括 executor 自己的执行线程).
         */
        virtual void Post(TTask Task) = 0;

        /**
         * @brief IsCurrentThread - 当前调用方线程是否就是此 executor 的执行线程.
         *
         * 用于 Get-redline / Post-redline 检测,避免在 executor 自己的执行线程上
         * 等待该 executor 上排队的 future(死锁风险)。
         *
         * Default = true(保守返回;具体 executor 应 override)。
         */
        virtual bool IsCurrentThread() const {
            return true;
        }
    };
} // namespace mession::async

// 兼容别名:旧代码仍可写 ITaskRunner,实际就是 IExecutor。
// 抽库完成后所有代码迁移到 IExecutor,本 typedef 会在后续 PR 删除。
using IExecutor_Compat = ::mession::async::IExecutor;