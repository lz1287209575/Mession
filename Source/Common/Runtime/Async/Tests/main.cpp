/**
 * @file main.cpp
 * @brief AsyncContext / MAsync 的运行时行为测试入口(Post 落到 loop、Get 红线等)。
 */
#include "Common/Runtime/Async/AsyncContext.h"
#include "Common/Runtime/Async/MAsync.h"
#include "Common/Runtime/Concurrency/Promise.h"
#include "Common/Runtime/EventLoop/TaskEventLoop.h"
#include "Common/Runtime/Log/Tests/TestHarness.h"

#include <future>
#include <thread>

namespace {
    MTaskEventLoop& SharedLoop() {
        static MTaskEventLoop Loop;
        return Loop;
    }
} // namespace

TEST_CASE(Async_ContextPostRunsOnLoop) {
    auto&                     Loop = SharedLoop();
    MAsync::MLoopAsyncContext Ctx(&Loop);
    bool                      bRan = false;
    Ctx.Post([&] { bRan = true; });
    EXPECT_TRUE(!bRan);
    Loop.RunOnce();
    EXPECT_TRUE(bRan);
}

TEST_CASE(Async_IsCurrentThread) {
    auto&                     Loop = SharedLoop();
    MAsync::MLoopAsyncContext Ctx(&Loop);
    Loop.RunOnce();
    EXPECT_TRUE(Ctx.IsSameContext());

    bool        bOtherThreadSame = true;
    std::thread T([&] { bOtherThreadSame = Ctx.IsSameContext(); });
    T.join();
    EXPECT_TRUE(!bOtherThreadSame);
}

TEST_CASE(Async_PendingFutureResolvesViaContext) {
    auto&                     Loop = SharedLoop();
    MAsync::MLoopAsyncContext Ctx(&Loop);
    MAsync::MAsyncContext::SetCurrent(&Ctx);

    MPromise<TResult<int, FAppError>> Promise;
    auto                              Future = Promise.GetFuture();
    EXPECT_TRUE(!Future.IsReady());

    Ctx.Post([&] { Promise.SetValue(TResult<int, FAppError>::Ok(42)); });
    EXPECT_TRUE(!Future.IsReady());
    Loop.RunOnce();
    EXPECT_TRUE(Future.IsReady());
    TResult<int, FAppError> R = Future.Get();
    EXPECT_TRUE(R.IsOk());
    EXPECT_EQ(R.GetValue(), 42);

    MAsync::MAsyncContext::SetCurrent(nullptr);
}

int main() {
    std::printf("Running MAsyncTest (P1: MAsyncContext + IsCurrentThread + Pending resolves via context)\n");

    std::printf("[ Async_ContextPostRunsOnLoop ]\n");
    Test_Async_ContextPostRunsOnLoop();
    std::printf("[ Async_IsCurrentThread ]\n");
    Test_Async_IsCurrentThread();
    std::printf("[ Async_PendingFutureResolvesViaContext ]\n");
    Test_Async_PendingFutureResolvesViaContext();

    // NOTE: Get 红线这条路径在这里有意不做布尔断言 —— 要把它踩出来,要么靠断言
    // 直接中止测试进程,要么去捕获 LOG_ERROR 的 stderr,两者对本切片来说都太重。
    // 红线本身由 pending-RPC 端到端路径覆盖,另有 Scripts/validate.py 套件单独验证。

    RUN_TESTS();
}
