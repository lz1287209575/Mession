/**
 * @file AsyncContext.cpp
 * @brief MAsyncContext 实现 —— TLS ambient 指针的读写,以及 MLoopAsyncContext 的 IExecutor 绑定。
 */
#include "Common/Runtime/Async/AsyncContext.h"

#include <cstdio>

namespace MAsync {

    namespace {
        // 每个线程一个 ambient 指针:Sub I/O 线程安装自己的 ambient,
        // Worker 线程不安装(改为显式传递 SubAmbient)。
        thread_local MAsyncContext* GCurrentContext = nullptr;
    } // namespace

    MAsyncContext* MAsyncContext::Current() {
        return GCurrentContext;
    }

    void MAsyncContext::SetCurrent(MAsyncContext* Ctx) {
        GCurrentContext = Ctx;
    }

    MLoopAsyncContext::MLoopAsyncContext(mession::async::IExecutor* InExecutor) : Executor(InExecutor) {
    }

    void MLoopAsyncContext::Post(TFunction<void()> Continuation) {
        if (Executor && Continuation) {
            Executor->Post(std::move(Continuation));
        } else if (Continuation) {
            std::fprintf(stderr, "MLoopAsyncContext::Post: null Executor; continuation dropped\n");
        }
    }

    bool MLoopAsyncContext::IsSameContext() const {
        return Executor ? Executor->IsCurrentThread() : false;
    }

} // namespace MAsync