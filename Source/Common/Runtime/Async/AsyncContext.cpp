#include "Common/Runtime/Async/AsyncContext.h"

#include <cstdio>

namespace MAsync {

    namespace {
        // TLS:每个线程一个 ambient指针
        // Sub I/O 线程安装自己的 ambient
        // Worker 线程不安装(显式传 SubAmbient)
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