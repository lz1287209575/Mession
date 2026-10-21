/**
 * @file AsyncContext.cpp
 * @brief MAsyncContext 实现 —— TLS ambient 指针的读写,以及 MLoopAsyncContext 的 IExecutor 绑定。
 */
#include "Common/Runtime/Async/AsyncContext.h"

#include <cstdio>

namespace MAsync {

    namespace {
        // 每个线程一个 ambient 指针。执行入口安装自己的 context；跨线程
        // 恢复由 await 注册点显式捕获并传递，不能在完成时重新读取 TLS。
        thread_local MAsyncContext* GCurrentContext = nullptr;
    } // namespace

    MAsyncContext* MAsyncContext::Current() {
        return GCurrentContext;
    }

    TSharedPtr<MAsyncContext> MAsyncContext::CurrentShared() {
        MAsyncContext* Context = Current();
        if (Context == nullptr) {
            return {};
        }
        try {
            return Context->shared_from_this();
        } catch (const std::bad_weak_ptr&) {
            // 测试或嵌入式调用方若把 context 放在栈上，不能把它交给跨线程
            // continuation；把它当作“未安装 context”处理，避免悬空引用。
            return {};
        }
    }

    void MAsyncContext::SetCurrent(MAsyncContext* Ctx) {
        GCurrentContext = Ctx;
    }

    MLoopAsyncContext::MLoopAsyncContext(mession::async::IExecutor* InExecutor) : Executor(InExecutor) {
    }

    void MLoopAsyncContext::Post(TFunction<void()> Continuation) {
        std::lock_guard<std::mutex> Lock(ExecutorMutex);
        if (Executor && Continuation) {
            Executor->Post(std::move(Continuation));
        } else if (Continuation) {
            std::fprintf(stderr, "MLoopAsyncContext::Post: null Executor; continuation dropped\n");
        }
    }

    bool MLoopAsyncContext::IsSameContext() const {
        std::lock_guard<std::mutex> Lock(ExecutorMutex);
        return Executor ? Executor->IsCurrentThread() : false;
    }

    void MLoopAsyncContext::Deactivate() {
        std::lock_guard<std::mutex> Lock(ExecutorMutex);
        Executor = nullptr;
    }

} // namespace MAsync
