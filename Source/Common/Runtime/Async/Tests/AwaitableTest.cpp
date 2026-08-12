#include "Common/Runtime/Log/Tests/TestHarness.h"
#include "Common/Runtime/Async/Awaitable.h"
#include "Common/Runtime/Async/MAsync.h"
#include "Common/Runtime/Concurrency/Promise.h"

#include <memory>

// P5 类型层 — SAwaiter 三方法 + TAwaitable 模板（KD-1 / KD-2）
//
// 迷你 Frame：只实现 codegen Frame 的 Resume() 契约（KD-6 业务侧不可见，
// 类型层只依赖 Resume()）。
namespace
{

struct FTestFrame
{
    int ResumeCount = 0;

    void Resume()
    {
        ++ResumeCount;
    }
};

SFutureResult<int> MakeReadyOk(int Value)
{
    MPromise<TResult<int, FAppError>> P;
    P.SetValue(TResult<int, FAppError>::Ok(Value));
    return SFutureResult<int>(P.GetFuture());
}

SFutureResult<int> MakeReadyErr()
{
    MPromise<TResult<int, FAppError>> P;
    P.SetValue(TResult<int, FAppError>::Err(FAppError{"test_err", "boom"}));
    return SFutureResult<int>(P.GetFuture());
}

SFutureResult<int> ComputeAsync(int Seed)
{
    return MakeReadyOk(Seed * 2);
}

TEST_CASE(SAwaiter_ReadyPath_AwaitReadyTrueAndResumeValue)
{
    auto Future = MakeReadyOk(21);
    auto Awaiter = Future.AsAwaiter();
    EXPECT_TRUE(Awaiter.AwaitReady());
    EXPECT_TRUE(Awaiter.AwaitResume() == 21);
}

TEST_CASE(SAwaiter_ErrPath_AwaitResumeThrows)
{
    auto Future = MakeReadyErr();
    auto Awaiter = Future.AsAwaiter();
    EXPECT_TRUE(Awaiter.AwaitReady());
    bool bThrew = false;
    try
    {
        (void)Awaiter.AwaitResume();
    }
    catch (const FFutureResultError& E)
    {
        bThrew = true;
        EXPECT_TRUE(MString(E.what()).find("boom") != MString::npos);
    }
    EXPECT_TRUE(bThrew);
}

TEST_CASE(SAwaiter_Suspend_ThenResumesFrameOnCompletion)
{
    MPromise<TResult<int, FAppError>> P;
    SFutureResult<int> Future(P.GetFuture());
    auto Awaiter = Future.AsAwaiter();
    EXPECT_TRUE(!Awaiter.AwaitReady());  // pending

    FTestFrame Frame;
    Awaiter.AwaitSuspend(&Frame);
    EXPECT_TRUE(Frame.ResumeCount == 0);  // 未完成前不恢复

    P.SetValue(TResult<int, FAppError>::Ok(7));  // 完成 → Then 回调 → Frame->Resume()
    EXPECT_TRUE(Frame.ResumeCount == 1);
}

TEST_CASE(TAwaitable_FormA_MinimalSignature)
{
    // 最简签名：TAwaitable<F>(args)——F 是 auto 非类型参数（函数名），
    // R 从函数指针返回类型（SFutureResult<R>::InnerType）推导。
    // AsAwaiter 由 codegen 注入（KD-6），类型层只验证构造/占位转换可编译。
    TAwaitable<ComputeAsync> Awaitable(5);
    const int Placeholder = Awaitable.operator int();
    EXPECT_TRUE(Placeholder == 0);
}

TEST_CASE(TAwaitable_FormA_ReturnConversion)
{
    // return TAwaitable<F>(args) 的占位转换（operator SFutureResult<R>）
    const auto F = static_cast<SFutureResult<int>>(TAwaitable<ComputeAsync>(3));
    EXPECT_TRUE(true);
}

}  // namespace
