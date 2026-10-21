/**
 * @file AsyncContext.h
 * @brief MAsyncContext —— 异步执行线抽象(AWAIT / Then 的续体投递目标)。
 */
#pragma once

#include "Common/Runtime/Async/IExecutor.h"
#include "Common/Runtime/MLib.h"

/**
 * @brief MAsyncContext - 异步上下文(规范 §9 的薄 SynchronizationContext)。
 *
 * @detail 一个 MAsyncContext 代表一条「执行线」；await 注册点捕获
 * Current() 并显式把它交给 future，完成线程不参与选择恢复目标。
 * 相比 .NET SynchronizationContext 只保留最小子集:
 *   * Post(Continuation) —— 把续体投递到此 context 的执行线
 *   * IsSameContext() —— Get 红线检测;true 表示当前线程就是执行线本身
 *   * Current()/SetCurrent() —— TLS ambient
 *
 * v1 的默认实现 MLoopAsyncContext 绑 mession::async::IExecutor(process
 * event loop)。IExecutor 接口定义在同目录 IExecutor.h,抽库后
 * mession-async 不依赖 mession-core。
 */

namespace MAsync {

    class MAsyncContext : public TEnableSharedFromThis<MAsyncContext> {
        public:
        virtual ~MAsyncContext() = default;

        /** 把续体投递到此 context 的执行线。可从任意线程调用。*/
        virtual void Post(TFunction<void()> Continuation) = 0;

        /** 当且仅当调用线程就是本 context 的执行线时返回 true。用于 Get 红线检测。*/
        virtual bool IsSameContext() const {
            return false;
        }

        /** 停止接收新续体；关停执行器前必须调用。*/
        virtual void Deactivate() {
        }

        /** 线程局部 ambient 指针;当前线程未安装时返回 nullptr。*/
        static MAsyncContext* Current();

        /**
         * 返回当前 ambient 的保活句柄。续体必须捕获此句柄，而不能保存
         * Current() 的裸指针；服务退出后，未完成 future 仍可能在其他线程兑现。
         * 未由 shared_ptr 管理的上下文返回空句柄。
         */
        static TSharedPtr<MAsyncContext> CurrentShared();

        /** 设置 TLS ambient(由执行入口安装;业务代码不要直接调)。*/
        static void SetCurrent(MAsyncContext* Ctx);
    };

    /** @brief 默认 context:绑到 mession::async::IExecutor(取代旧的 ITaskRunner)。*/
    class MLoopAsyncContext final : public MAsyncContext {
        public:
        explicit MLoopAsyncContext(mession::async::IExecutor* InExecutor);

        void Post(TFunction<void()> Continuation) override;
        bool IsSameContext() const override; // 等价于 Executor->IsCurrentThread()
        void Deactivate() override;

        private:
        // Post 与 Deactivate 互斥，确保执行器释放后不会再被晚到的 future 访问。
        mutable std::mutex           ExecutorMutex;
        mession::async::IExecutor*   Executor = nullptr; // 不持有所有权
    };

} // namespace MAsync
