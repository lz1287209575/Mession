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

    // Note: Get-redline path (Sub-task 8) is intentionally not exercised as a
    // boolean test here — exercising it requires either aborting the test
    // process via assertion, or capturing stderr for LOG_ERROR, both of which
    // are heavier than warranted for this slice. The redline itself is
    // exercised in the pending-RPC end-to-end path (validated separately
    // via Scripts/validate.py suites).

    RUN_TESTS();
}
