/**
 * @file main.cpp
 * @brief AsyncDemo 的冒烟测试 —— 场景与 AsyncDemo.cpp 的 main() 一致,
 *        改用 TestHarness.h 的 TEST_CASE + EXPECT_TRUE 写法。
 */
#include "Examples/AsyncDemo/AsyncDemo.h"

#include "Common/Runtime/Concurrency/Promise.h"
#include "Common/Runtime/Log/Tests/TestHarness.h"
#include "Common/Runtime/Object/Result.h"

#include <cstdio>

namespace {

    // Ready-Ok 辅助函数(与 AsyncFrameTest 的写法一致):每次调用产生一个新的
    // future,保证各用例之间互不影响。
    SFutureResult<int> MakeReadyOk(int Value) {
        MPromise<TResult<int, FAppError>> P;
        P.SetValue(TResult<int, FAppError>::Ok(Value));
        return SFutureResult<int>(P.GetFuture());
    }

} // namespace

TEST_CASE(AsyncDemo_ComputeAsync_DoublesSeed) {
    auto F = AsyncDemoNS::ComputeAsync(21);
    EXPECT_TRUE(F.IsReady());
    EXPECT_TRUE(F.IsOk());
    EXPECT_TRUE(F.GetResult().GetValue() == 42);
}

TEST_CASE(AsyncDemo_ChainAsync_AddsOne) {
    auto F = AsyncDemoNS::ChainAsync(21);
    EXPECT_TRUE(F.IsReady());
    EXPECT_TRUE(F.IsOk());
    EXPECT_TRUE(F.GetResult().GetValue() == 43);
}

TEST_CASE(AsyncDemo_FrameCapturesAwaitedSlot) {
    // P4 v1 的 AwaitOk 是直通实现:Awaited 会被原样记进 AwaitedSlot。因此经由
    // Frame 走一遍后,AwaitedSlot 应当就绪,且与被 await 的 future 一致。
    auto               Frame   = MakeShared<MHeaderTool_AsyncFrame_Free_ComputeAsync>();
    SFutureResult<int> Awaited = MakeReadyOk(8);

    SFutureResult<int> Returned = AWAIT_OK(std::move(Awaited));

    EXPECT_TRUE(Frame->AwaitedSlot.IsReady());
    EXPECT_TRUE(Returned.IsReady());
    EXPECT_TRUE(Frame->AwaitedSlot.PeekResult().GetValue() == Returned.PeekResult().GetValue());
    EXPECT_TRUE(Frame->StoredValue == 8);
}

TEST_CASE(AsyncDemo_SyncBarrier_WaitOnMainThread) {
    // 在测试线程(无事件循环)里走同步屏障。用来验证:当没有 current
    // MAsyncContext 时,P1 §8.2 的红线不会被触发 —— 也就是说,脱离事件循环
    // 调 F.Wait() / F.Get() 是被支持的用法。
    auto F = AsyncDemoNS::ChainAsync(7);
    F.Wait();
    EXPECT_TRUE(F.IsReady());
    EXPECT_TRUE(F.IsOk());
    EXPECT_TRUE(F.GetResult().GetValue() == 15); // (7*2) + 1
}

int main() {
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