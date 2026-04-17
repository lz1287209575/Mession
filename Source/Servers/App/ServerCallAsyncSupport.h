#pragma once

#include "Common/Runtime/Concurrency/Async.h"
#include "Common/Runtime/Concurrency/Promise.h"
#include "Common/Net/Rpc/RpcServerCall.h"
#include "Common/Runtime/Object/Result.h"
#include "Servers/App/FiberCallSupport.h"
#include "Servers/App/ResultFutureSupport.h"

#include <exception>
#include <functional>
#include <type_traits>

namespace MServerCallAsyncSupport
{
template<typename TResponse>
using TAppResult = TResult<TResponse, FAppError>;

template<typename TResponse>
using TAppFuture = MFuture<TAppResult<TResponse>>;

namespace MDetail
{
inline FAppError MakeAsyncExceptionError(const char* Message)
{
    return FAppError::Make("server_call_async_exception", Message ? Message : "unknown");
}

template<typename TResponse>
TAppFuture<TResponse> MakeExceptionFuture(const char* Message)
{
    return MAppResultFutureSupport::MakeReadyFuture(MakeErrorResult<TResponse>(MakeAsyncExceptionError(Message)));
}
}

template<typename TResponse>
TAppFuture<TResponse> MakeResultFuture(TAppResult<TResponse> Result)
{
    return MAppResultFutureSupport::MakeReadyFuture(std::move(Result));
}

template<typename TResponse>
TAppFuture<TResponse> MakeSuccessFuture(TResponse Response)
{
    return MakeResultFuture(TAppResult<TResponse>::Ok(std::move(Response)));
}

template<typename TResponse>
TAppFuture<TResponse> MakeErrorFuture(const char* Code, const char* Message = "")
{
    return MakeResultFuture(MakeErrorResult<TResponse>(FAppError::Make(
        Code ? Code : "server_call_failed",
        Message ? Message : "")));
}

template<typename TResponse>
bool StartDeferredServerCall(
    const SServerCallContext& Context,
    TAppFuture<TResponse> Future,
    const char* FunctionName = nullptr)
{
    if (!Context.IsValid())
    {
        return false;
    }

    return MAppResultFutureSupport::ObserveResultFuture<TResponse>(
        std::move(Future),
        [Context, FunctionName]() mutable
        {
            return SendDeferredServerCallErrorResponse(
                Context,
                FAppError::Make("server_call_invalid_future", FunctionName ? FunctionName : "ServerCall"));
        },
        [Context](const TAppResult<TResponse>& Result) mutable
        {
            if (Result.IsOk())
            {
                (void)SendDeferredServerCallSuccessResponse(Context, BuildPayload(Result.GetValue()));
                return;
            }

            (void)SendDeferredServerCallErrorResponse(Context, Result.GetError());
        },
        [Context](const char* Message) mutable
        {
            (void)SendDeferredServerCallErrorResponse(
                Context,
                FAppError::Make("server_call_exception", Message ? Message : "unknown"));
        });
}

template<typename TResponse, typename TBody>
TAppFuture<TResponse> StartFiber(
    ITaskRunner* Runner,
    TBody&& Body)
{
    using TBodyValue = std::decay_t<TBody>;
    TBodyValue BodyValue(std::forward<TBody>(Body));

    MPromise<TAppResult<TResponse>> Promise;
    TAppFuture<TResponse> Future = Promise.GetFuture();
    (void)MAppFiberCallSupport::StartDetachedResultFiber<TAppResult<TResponse>>(
        "ServerCallFiber",
        Runner,
        std::move(BodyValue),
        [Promise](TAppResult<TResponse> Result) mutable
        {
            Promise.SetValue(std::move(Result));
        },
        [Promise](MAppFiberCallSupport::EFiberFailureKind FailureKind, const char* Detail) mutable
        {
            switch (FailureKind)
            {
            case MAppFiberCallSupport::EFiberFailureKind::RunnerMissing:
                Promise.SetValue(MakeErrorResult<TResponse>(
                    MDetail::MakeAsyncExceptionError(Detail)));
                return;
            case MAppFiberCallSupport::EFiberFailureKind::CreateFailed:
                Promise.SetValue(MakeErrorResult<TResponse>(
                    MDetail::MakeAsyncExceptionError("server_call_fiber_create_failed")));
                return;
            case MAppFiberCallSupport::EFiberFailureKind::StartFailed:
                Promise.SetValue(MakeErrorResult<TResponse>(
                    FAppError::Make("server_call_fiber_start_failed", Detail ? Detail : "unknown")));
                return;
            case MAppFiberCallSupport::EFiberFailureKind::BodyException:
            case MAppFiberCallSupport::EFiberFailureKind::UnhandledException:
                Promise.SetValue(MakeErrorResult<TResponse>(
                    FAppError::Make("server_call_exception", Detail ? Detail : "unknown")));
                return;
            }
        });

    return Future;
}

/** 单步映射：适合“一个异步结果 -> 一个同步响应”的短路径。 */
template<typename TInputResponse, typename TMapper, typename TOutputResponse = std::decay_t<std::invoke_result_t<std::decay_t<TMapper>, const TInputResponse&>>>
TAppFuture<TOutputResponse> Map(TAppFuture<TInputResponse> Future, TMapper&& Mapper)
{
    return MAppResultFutureSupport::MapWithErrorPolicy(
        std::move(Future),
        std::forward<TMapper>(Mapper),
        [](const char* Message) { return MDetail::MakeExceptionFuture<TOutputResponse>(Message); },
        [](const char* Message)
        {
            return MDetail::MakeAsyncExceptionError(Message);
        });
}

template<typename TResponse, typename TObserver>
TAppFuture<TResponse> TapSuccess(TAppFuture<TResponse> Future, TObserver&& Observer)
{
    return MAppResultFutureSupport::TapSuccess(std::move(Future), std::forward<TObserver>(Observer));
}

/** 短链编排：适合 2 步以内的异步串联。 */
template<
    typename TInputResponse,
    typename TBinder,
    typename TNextFuture = std::decay_t<std::invoke_result_t<std::decay_t<TBinder>, const TInputResponse&>>,
    typename TOutputResponse = typename MAppResultFutureSupport::TResultFutureTraits<TNextFuture>::TResponseType>
TAppFuture<TOutputResponse> Chain(TAppFuture<TInputResponse> Future, TBinder&& Binder)
{
    return MAppResultFutureSupport::ChainWithErrorPolicy(
        std::move(Future),
        std::forward<TBinder>(Binder),
        [](const char* Message) { return MDetail::MakeExceptionFuture<TOutputResponse>(Message); },
        [](const char* Message)
        {
            return MDetail::MakeAsyncExceptionError(Message);
        });
}

template<
    typename TInputResponse,
    typename TBinder,
    typename TNextFuture = std::decay_t<std::invoke_result_t<std::decay_t<TBinder>, const TInputResponse&>>,
    typename TOutputResponse = typename MAppResultFutureSupport::TResultFutureTraits<TNextFuture>::TResponseType>
TAppFuture<TOutputResponse> ThenOk(TAppFuture<TInputResponse> Future, TBinder&& Binder)
{
    return Chain(std::move(Future), std::forward<TBinder>(Binder));
}
}
