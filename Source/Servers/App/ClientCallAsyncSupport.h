#pragma once

#include "Common/Runtime/Concurrency/Async.h"
#include "Common/Net/Rpc/RpcClientCall.h"
#include "Common/Runtime/Concurrency/Promise.h"
#include "Common/Runtime/Object/Result.h"
#include "Protocol/Messages/Common/AppMessages.h"
#include "Servers/App/FiberCallSupport.h"
#include "Servers/App/ResultFutureSupport.h"

#include <exception>
#include <functional>
#include <type_traits>
#include <utility>

namespace MClientCallAsyncSupport
{
template<typename TResponse>
using TAppResult = TResult<TResponse, FAppError>;

template<typename TResponse>
using TAppFuture = MFuture<TAppResult<TResponse>>;

namespace MDetail
{
inline FAppError MakeAsyncExceptionError(const char* Message)
{
    return FAppError::Make("client_call_async_exception", Message ? Message : "unknown");
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
        Code ? Code : "client_call_failed",
        Message ? Message : "")));
}

template<typename TResponse, typename TErrorMapper>
bool StartDeferred(
    const SClientCallContext& Context,
    TAppFuture<TResponse> Future,
    TErrorMapper&& ErrorMapper)
{
    if (!Context.IsValid())
    {
        return false;
    }

    RegisterDeferredClientCall(Context);
    return MAppResultFutureSupport::ObserveResultFuture<TResponse>(
        std::move(Future),
        [Context, ErrorMapper = std::forward<TErrorMapper>(ErrorMapper)]() mutable
        {
            TResponse Failed = ErrorMapper(MDetail::MakeAsyncExceptionError("invalid_future"));
            return SendDeferredClientCallResponse(Context, Failed);
        },
        [Context, ErrorMapper](const TAppResult<TResponse>& Result) mutable
        {
            TResponse Response {};
            if (Result.IsOk())
            {
                Response = Result.GetValue();
            }
            else
            {
                Response = ErrorMapper(Result.GetError());
            }

            (void)SendDeferredClientCallResponse(Context, Response);
        },
        [Context, ErrorMapper](const char* Message) mutable
        {
            TResponse Failed = ErrorMapper(FAppError::Make("client_call_exception", Message ? Message : "unknown"));
            (void)SendDeferredClientCallResponse(Context, Failed);
        });
}

template<typename TResponse>
bool StartDeferred(const SClientCallContext& Context, TAppFuture<TResponse> Future)
{
    return StartDeferred<TResponse>(
        Context,
        std::move(Future),
        [](const FAppError&)
        {
            return TResponse {};
        });
}

template<typename TResponse, typename TBody, typename TErrorMapper>
bool StartDeferredFiber(
    const SClientCallContext& Context,
    ITaskRunner* Runner,
    TBody&& Body,
    TErrorMapper&& ErrorMapper)
{
    if (!Context.IsValid())
    {
        return false;
    }

    RegisterDeferredClientCall(Context);

    using TBodyValue = std::decay_t<TBody>;
    using TErrorMapperValue = std::decay_t<TErrorMapper>;
    TBodyValue BodyValue(std::forward<TBody>(Body));
    TErrorMapperValue MapperValue(std::forward<TErrorMapper>(ErrorMapper));

    auto SendFailure =
        [Context, ErrorMapper = MapperValue](const FAppError& Error) mutable
        {
            TResponse Failed = ErrorMapper(Error);
            (void)SendDeferredClientCallResponse(Context, Failed);
        };

    return MAppFiberCallSupport::StartDetachedResultFiber<TAppResult<TResponse>>(
        "ClientCallDeferred",
        Runner,
        std::move(BodyValue),
        [Context, ErrorMapper = MapperValue](TAppResult<TResponse> Result) mutable
        {
            TResponse Response {};
            if (Result.IsOk())
            {
                Response = Result.GetValue();
            }
            else
            {
                Response = ErrorMapper(Result.GetError());
            }

            (void)SendDeferredClientCallResponse(Context, Response);
        },
        [SendFailure = std::move(SendFailure), Context, ErrorMapper = MapperValue](
            MAppFiberCallSupport::EFiberFailureKind FailureKind,
            const char* Detail) mutable
        {
            switch (FailureKind)
            {
            case MAppFiberCallSupport::EFiberFailureKind::RunnerMissing:
                {
                    TResponse Failed = ErrorMapper(MDetail::MakeAsyncExceptionError(Detail));
                    (void)SendDeferredClientCallResponse(Context, Failed);
                    return;
                }
            case MAppFiberCallSupport::EFiberFailureKind::CreateFailed:
                {
                    TResponse Failed = ErrorMapper(MDetail::MakeAsyncExceptionError("client_call_fiber_create_failed"));
                    (void)SendDeferredClientCallResponse(Context, Failed);
                    return;
                }
            case MAppFiberCallSupport::EFiberFailureKind::StartFailed:
                SendFailure(FAppError::Make("client_call_fiber_start_failed", Detail ? Detail : "unknown"));
                return;
            case MAppFiberCallSupport::EFiberFailureKind::BodyException:
            case MAppFiberCallSupport::EFiberFailureKind::UnhandledException:
                SendFailure(FAppError::Make("client_call_exception", Detail ? Detail : "unknown"));
                return;
            }
        });
}

template<typename TResponse, typename TBody>
bool StartDeferredFiber(
    const SClientCallContext& Context,
    ITaskRunner* Runner,
    TBody&& Body)
{
    return StartDeferredFiber<TResponse>(
        Context,
        Runner,
        std::forward<TBody>(Body),
        [](const FAppError&)
        {
            return TResponse {};
        });
}

template<typename TInputResponse, typename TMapper, typename TOutputResponse = std::decay_t<std::invoke_result_t<std::decay_t<TMapper>, const TInputResponse&>>>
TAppFuture<TOutputResponse> Map(TAppFuture<TInputResponse> Future, TMapper&& Mapper)
{
    return MAppResultFutureSupport::MapWithErrorPolicy(
        std::move(Future),
        std::forward<TMapper>(Mapper),
        [](const char* Message) { return MakeErrorFuture<TOutputResponse>("client_call_async_exception", Message); },
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
TAppFuture<TOutputResponse> Chain(TAppFuture<TInputResponse> Future, TBinder&& Binder)
{
    return MAppResultFutureSupport::ChainWithErrorPolicy(
        std::move(Future),
        std::forward<TBinder>(Binder),
        [](const char* Message) { return MakeErrorFuture<TOutputResponse>("client_call_async_exception", Message); },
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
