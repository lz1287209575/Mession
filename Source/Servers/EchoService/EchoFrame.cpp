#include "Servers/EchoService/EchoFrame.h"

#include "Common/Net/Rpc/MRpcChannel.h"
#include "Common/Runtime/Async/AsyncContext.h"
#include "Common/Runtime/Log/Log.h"
#include "Servers/EchoService/EchoService.h"

FEchoFrame::FEchoFrame()
    : OuterFuture(OuterPromise.GetFuture())
{
}

template<typename T>
T FEchoFrame::AwaitOk(SFutureResult<T> Awaited)
{
    static_assert(std::is_same_v<T, FSampleEchoResponse>,
                  "FEchoFrame::AwaitOk: P2 single-await v1 only supports FSampleEchoResponse");

    if (Awaited.IsReady())
    {
        TResult<T, FAppError> R = Awaited.GetResult();
        if (R.IsErr())
        {
            PendingError = TResult<FSampleEchoResponse, FAppError>::Err(R.GetError());
            bHalted = true;
            return T{};
        }
        return R.GetValue();
    }

    // Pending: stash + 续体 + Run 会立刻 return Outer(pending)。
    Pending = Awaited;   // AwaitOk<T=FSampleEchoResponse> 单特化,赋值安全。
    Awaited.Then(
        [this](MFuture<TResult<T, FAppError>> /*F*/)
        {
            if (auto* Ctx = MAsync::MAsyncContext::Current())
            {
                Ctx->Post([this]{ this->Resume(); });
            }
            else
            {
                this->Resume();
            }
        });
    return T{};
}

// explicit instantiation for the single supported T
template FSampleEchoResponse FEchoFrame::AwaitOk<FSampleEchoResponse>(SFutureResult<FSampleEchoResponse>);

SFutureResult<FSampleEchoResponse> FEchoFrame::Run(const FSampleEchoRequest& InRequest,
                                                   MEchoService* InService)
{
    Service = InService;
    Request = InRequest;
    Ctx = MAsync::MAsyncContext::Current();

    // AWAIT_OK macro expands to `Frame->AwaitOk(expr)`. Bind `Frame` to `this`
    // (P2: single-await v1 owns a single frame object; Frame lifetime is bound
    // to the run-time call, but MEchoService::EchoAwait keeps us alive via
    // TSharedPtr so this pointer remains valid through any pending hops).
    FEchoFrame* Frame = this;

    // AWAIT_OK 宏展开: Frame->AwaitOk<FSampleEchoResponse>(expr)
    FSampleEchoResponse Resp = AWAIT_OK(
        MRpcChannel::Get().CallToActor<FSampleEchoResponse>(
            Request.TargetActorId, "MEchoService", "Echo", Request));

    // Tail mirrors the production Run contract; extracted as a public method
    // so the unit test can drive the same path without MRpcChannel.
    CompleteAfterAwaitForTest(std::move(Resp));

    return Outer();
}

void FEchoFrame::Resume()
{
    if (bHalted) return;   // 幂等
    if (!Pending.IsReady())
    {
        // 防御: Then 触发时 F 应已 ready;不应走到这里。
        return;
    }

    TResult<FSampleEchoResponse, FAppError> R = Pending.GetResult();
    if (R.IsErr())
    {
        OuterPromise.SetValue(
            TResult<FSampleEchoResponse, FAppError>::Err(R.GetError()));
    }
    else
    {
        OuterPromise.SetValue(
            TResult<FSampleEchoResponse, FAppError>::Ok(std::move(R).GetValue()));
    }
}