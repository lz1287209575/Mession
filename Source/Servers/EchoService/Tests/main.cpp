#include "Common/Runtime/Log/Tests/TestHarness.h"
#include "Common/Runtime/Async/AsyncContext.h"
#include "Common/Runtime/Async/MAsync.h"
#include "Common/Runtime/Async/AwaitMacros.h"
#include "Common/Runtime/EventLoop/TaskEventLoop.h"
#include "Common/Runtime/Concurrency/Promise.h"
#include "Servers/EchoService/EchoFrame.h"
#include "Protocol/Messages/EchoService/FSampleEchoMessages.h"

#include <utility>

namespace
{
MTaskEventLoop& SharedLoop()
{
    static MTaskEventLoop Loop;
    return Loop;
}

SFutureResult<FSampleEchoResponse> MakeReadyOk(const MString& EchoText)
{
    FSampleEchoResponse R;
    R.Echo = EchoText;
    R.SourceActorId = 0xCAFE;
    R.SourceServerName = "MockService";
    MPromise<TResult<FSampleEchoResponse, FAppError>> P;
    auto F = P.GetFuture();
    P.SetValue(TResult<FSampleEchoResponse, FAppError>::Ok(std::move(R)));
    return SFutureResult<FSampleEchoResponse>(std::move(F));
}

SFutureResult<FSampleEchoResponse> MakeReadyErr(const char* Code, const char* Msg)
{
    MPromise<TResult<FSampleEchoResponse, FAppError>> P;
    auto F = P.GetFuture();
    P.SetValue(TResult<FSampleEchoResponse, FAppError>::Err(FAppError::Make(Code, Msg)));
    return SFutureResult<FSampleEchoResponse>(std::move(F));
}
}

// Frame.Run uses MRpcChannel::Get().CallToActor<...> directly. To unit-test
// the Frame state machine without real network, we drive AwaitOk + the
// CompleteAfterAwaitForTest seam (same path Run uses for the sync tail).
// AWAIT_OK macro expansion is verified by passing a movable expression through
// it (the local `Frame` symbol must exist for macro to expand — that's the
// P2 contract).
TEST_CASE(AsyncFrame_AwaitOk_ReadyOk)
{
    auto Frame = MakeShared<FEchoFrame>();
    EXPECT_TRUE(!Frame->IsHalted());
    EXPECT_TRUE(!Frame->PendingIsSet());
    EXPECT_TRUE(!Frame->Outer().IsReady());

    // AWAIT_OK macro expands to Frame->AwaitOk(expr).
    FSampleEchoResponse Resp = AWAIT_OK(MakeReadyOk("ready-ok"));

    EXPECT_TRUE(Resp.Echo == "ready-ok");
    EXPECT_TRUE(Resp.SourceServerName == "MockService");
    EXPECT_TRUE(!Frame->IsHalted());
    EXPECT_TRUE(!Frame->PendingIsSet());

    // Complete the post-await tail.
    Frame->CompleteAfterAwaitForTest(std::move(Resp));

    EXPECT_TRUE(Frame->Outer().IsReady());
    auto R = Frame->Outer().GetResult();
    EXPECT_TRUE(R.IsOk());
    EXPECT_TRUE(R.GetValue().Echo == "ready-ok");
}

TEST_CASE(AsyncFrame_AwaitOk_ReadyErr_OuterErr)
{
    auto Frame = MakeShared<FEchoFrame>();
    EXPECT_TRUE(!Frame->IsHalted());

    FSampleEchoResponse Resp = AWAIT_OK(MakeReadyErr("mock_err", "simulated failure"));
    (void)Resp;

    // AwaitOk returns T{} on Err; Frame is halted, Pending not set.
    EXPECT_TRUE(Frame->IsHalted());
    EXPECT_TRUE(!Frame->PendingIsSet());

    // Halt path: CompleteAfterAwaitForTest propagates PendingError to Outer.
    Frame->CompleteAfterAwaitForTest(FSampleEchoResponse{});

    EXPECT_TRUE(Frame->Outer().IsReady());
    auto R = Frame->Outer().GetResult();
    EXPECT_TRUE(R.IsErr());
    EXPECT_TRUE(R.GetError().Code == "mock_err");
}

TEST_CASE(AsyncFrame_AwaitOk_PendingResolvesViaPost)
{
    auto& Loop = SharedLoop();
    MAsync::MLoopAsyncContext Ctx(&Loop);
    MAsync::MAsyncContext::SetCurrent(&Ctx);

    auto Frame = MakeShared<FEchoFrame>();
    EXPECT_TRUE(!Frame->Outer().IsReady());

    // Build a pending future, hold the promise to resolve later.
    MPromise<TResult<FSampleEchoResponse, FAppError>> PendingPromise;
    auto PendingFuture = PendingPromise.GetFuture();
    SFutureResult<FSampleEchoResponse> Pending(std::move(PendingFuture));

    // AWAIT_OK pending path: returns T{}, registers Then+Ctx->Post(Resume).
    FSampleEchoResponse Resp = AWAIT_OK(std::move(Pending));
    (void)Resp;

    EXPECT_TRUE(!Frame->IsHalted());
    EXPECT_TRUE(Frame->PendingIsSet());
    EXPECT_TRUE(!Frame->Outer().IsReady());

    // External code resolves the awaited future.
    FSampleEchoResponse R;
    R.Echo = "pending-resolved";
    R.SourceActorId = 0xBEEF;
    R.SourceServerName = "MockPending";
    PendingPromise.SetValue(TResult<FSampleEchoResponse, FAppError>::Ok(std::move(R)));

    // Then posted Resume; drain the loop to run it.
    Loop.RunOnce();

    EXPECT_TRUE(Frame->Outer().IsReady());
    auto OuterR = Frame->Outer().GetResult();
    EXPECT_TRUE(OuterR.IsOk());
    EXPECT_TRUE(OuterR.GetValue().Echo == "pending-resolved");

    MAsync::MAsyncContext::SetCurrent(nullptr);
}

TEST_CASE(AsyncFrame_AwaitOk_PendingLoopNotBlocked)
{
    auto& Loop = SharedLoop();
    MAsync::MLoopAsyncContext Ctx(&Loop);
    MAsync::MAsyncContext::SetCurrent(&Ctx);

    auto Frame = MakeShared<FEchoFrame>();

    MPromise<TResult<FSampleEchoResponse, FAppError>> Promise;
    auto PendingFuture = Promise.GetFuture();
    SFutureResult<FSampleEchoResponse> Pending(std::move(PendingFuture));

    // AWAIT_OK must NOT block the loop. We assert that immediately after
    // AwaitOk returns, the loop remains responsive to other Post()ed work.
    int OtherTaskRan = 0;
    FSampleEchoResponse Resp = AWAIT_OK(std::move(Pending));
    (void)Resp;

    Ctx.Post([&]{ OtherTaskRan = 1; });
    EXPECT_TRUE(OtherTaskRan == 0);

    // Drain the loop — should fire both the await-completion (Promise resolved
    // further below would be needed for that; for now just the Posted task)
    // and our posted OtherTask. We don't resolve Promise here; just check
    // that the loop can still drain independently.
    Loop.RunOnce();
    EXPECT_TRUE(OtherTaskRan == 1);

    MAsync::MAsyncContext::SetCurrent(nullptr);
}

int main()
{
    std::printf("Running AsyncFrameTest (P2: FEchoFrame AWAIT_OK state machine)\n");

    std::printf("[ AsyncFrame_AwaitOk_ReadyOk ]\n");
    Test_AsyncFrame_AwaitOk_ReadyOk();

    std::printf("[ AsyncFrame_AwaitOk_ReadyErr_OuterErr ]\n");
    Test_AsyncFrame_AwaitOk_ReadyErr_OuterErr();

    std::printf("[ AsyncFrame_AwaitOk_PendingResolvesViaPost ]\n");
    Test_AsyncFrame_AwaitOk_PendingResolvesViaPost();

    std::printf("[ AsyncFrame_AwaitOk_PendingLoopNotBlocked ]\n");
    Test_AsyncFrame_AwaitOk_PendingLoopNotBlocked();

    RUN_TESTS();
}
