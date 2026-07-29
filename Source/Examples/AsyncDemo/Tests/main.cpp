/**
 * @file main.cpp
 * @brief AsyncDemo smoke tests - same scenario as AsyncDemo.cpp's main(),
 *        wrapped in TEST_CASE + EXPECT_TRUE for the TestHarness.h pattern.
 */
#include "Examples/AsyncDemo/AsyncDemo.h"

#include "Common/Runtime/Log/Tests/TestHarness.h"
#include "Common/Runtime/Concurrency/Promise.h"
#include "Common/Runtime/Object/Result.h"

#include <cstdio>

namespace
{

// Ready-Ok helper mirroring the AsyncFrameTest pattern (P3) — isolated
// so each test case gets a fresh future.
SFutureResult<int> MakeReadyOk(int Value)
{
    MPromise<TResult<int, FAppError>> P;
    P.SetValue(TResult<int, FAppError>::Ok(Value));
    return SFutureResult<int>(P.GetFuture());
}

} // namespace

TEST_CASE(AsyncDemo_ComputeAsync_DoublesSeed)
{
    auto F = AsyncDemoNS::ComputeAsync(21);
    EXPECT_TRUE(F.IsReady());
    EXPECT_TRUE(F.IsOk());
    EXPECT_TRUE(F.GetResult().GetValue() == 42);
}

TEST_CASE(AsyncDemo_ChainAsync_AddsOne)
{
    auto F = AsyncDemoNS::ChainAsync(21);
    EXPECT_TRUE(F.IsReady());
    EXPECT_TRUE(F.IsOk());
    EXPECT_TRUE(F.GetResult().GetValue() == 43);
}

TEST_CASE(AsyncDemo_FrameCapturesAwaitedSlot)
{
    // P4 v1 pass-through AwaitOk: ConstructFrame records Awaited verbatim
    // into AwaitedSlot, so calling ComputeAsync through the Frame surface
    // should leave AwaitedSlot ready and matching the awaited future.
    auto Frame = MakeShared<MHeaderTool_AsyncFrame_Free_ComputeAsync>();
    SFutureResult<int> Awaited = MakeReadyOk(8);

    SFutureResult<int> Returned = AWAIT_OK(std::move(Awaited));

    EXPECT_TRUE(Frame->AwaitedSlot.IsReady());
    EXPECT_TRUE(Returned.IsReady());
    EXPECT_TRUE(Frame->AwaitedSlot.PeekResult().GetValue() ==
                Returned.PeekResult().GetValue());
    EXPECT_TRUE(Frame->StoredValue == 8);
}

TEST_CASE(AsyncDemo_SyncBarrier_WaitOnMainThread)
{
    // Sync barrier exercised from the test runner thread (no event loop).
    // Verifies the P1 §8.2 redline does not fire when no MAsyncContext is
    // current — i.e. running F.Wait()/F.Get() off-loop is the supported path.
    auto F = AsyncDemoNS::ChainAsync(7);
    F.Wait();
    EXPECT_TRUE(F.IsReady());
    EXPECT_TRUE(F.IsOk());
    EXPECT_TRUE(F.GetResult().GetValue() == 15); // (7*2) + 1
}

int main()
{
    std::printf("Running AsyncDemoTest (P4 wrap example smoke)\n");

    std::printf("[ AsyncDemo_ComputeAsync_DoublesSeed ]\n");
    Test_AsyncDemo_ComputeAsync_DoublesSeed();

    std::printf("[ AsyncDemo_ChainAsync_AddsOne ]\n");
    Test_AsyncDemo_ChainAsync_AddsOne();

    std::printf("[ AsyncDemo_FrameCapturesAwaitedSlot ]\n");
    Test_AsyncDemo_FrameCapturesAwaitedSlot();

    std::printf("[ AsyncDemo_SyncBarrier_WaitOnMainThread ]\n");
    Test_AsyncDemo_SyncBarrier_WaitOnMainThread();

    RUN_TESTS();
}