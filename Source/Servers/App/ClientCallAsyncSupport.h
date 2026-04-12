#pragma once

#include "Common/Runtime/Concurrency/Async.h"
#include "Common/Net/Rpc/RpcClientCall.h"
#include "Common/Runtime/Concurrency/Promise.h"
#include "Common/Runtime/Object/Result.h"
#include "Protocol/Messages/Common/AppMessages.h"

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
template<typename TFuture>
struct TAppFutureTraits;

template<typename TResponse>
struct TAppFutureTraits<MFuture<TResult<TResponse, FAppError>>>
{
    using TResponseType = TResponse;
};

inline FAppError MakeAsyncExceptionError(const char* Message)
{
    return FAppError::Make("client_call_async_exception", Message ? Message : "unknown");
}
}

template<typename TResponse>
TAppFuture<TResponse> MakeResultFuture(TAppResult<TResponse> Result)
{
    MPromise<TAppResult<TResponse>> Promise;
    TAppFuture<TResponse> Future = Promise.GetFuture();
    Promise.SetValue(std::move(Result));
    return Future;
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

    if (!Future.Valid())
    {
        RegisterDeferredClientCall(Context);
        TResponse Failed = ErrorMapper(MDetail::MakeAsyncExceptionError("invalid_future"));
        return SendDeferredClientCallResponse(Context, Failed);
    }

    RegisterDeferredClientCall(Context);
    Future.Then(
        [Context, ErrorMapper = std::forward<TErrorMapper>(ErrorMapper)](TAppFuture<TResponse> Completed) mutable
        {
            try
            {
                const TAppResult<TResponse> Result = Completed.Get();
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
            }
            catch (const std::exception& Ex)
            {
                TResponse Failed = ErrorMapper(FAppError::Make("client_call_exception", Ex.what()));
                (void)SendDeferredClientCallResponse(Context, Failed);
            }
            catch (...)
            {
                TResponse Failed = ErrorMapper(FAppError::Make("client_call_exception", "unknown"));
                (void)SendDeferredClientCallResponse(Context, Failed);
            }
        });
    return true;
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

template<typename TInputResponse, typename TMapper, typename TOutputResponse = std::decay_t<std::invoke_result_t<std::decay_t<TMapper>, const TInputResponse&>>>
TAppFuture<TOutputResponse> Map(TAppFuture<TInputResponse> Future, TMapper&& Mapper)
{
    if (!Future.Valid())
    {
        return MakeErrorFuture<TOutputResponse>("client_call_async_exception", "invalid_future");
    }

    MPromise<TAppResult<TOutputResponse>> Promise;
    TAppFuture<TOutputResponse> OutFuture = Promise.GetFuture();
    Future.Then(
        [Promise, Mapper = std::forward<TMapper>(Mapper)](TAppFuture<TInputResponse> Completed) mutable
        {
            try
            {
                const TAppResult<TInputResponse> Result = Completed.Get();
                if (!Result.IsOk())
                {
                    Promise.SetValue(MakeErrorResult<TOutputResponse>(Result.GetError()));
                    return;
                }

                Promise.SetValue(TAppResult<TOutputResponse>::Ok(std::invoke(Mapper, Result.GetValue())));
            }
            catch (const std::exception& Ex)
            {
                Promise.SetValue(MakeErrorResult<TOutputResponse>(MDetail::MakeAsyncExceptionError(Ex.what())));
            }
            catch (...)
            {
                Promise.SetValue(MakeErrorResult<TOutputResponse>(MDetail::MakeAsyncExceptionError("unknown")));
            }
        });
    return OutFuture;
}

template<
    typename TInputResponse,
    typename TBinder,
    typename TNextFuture = std::decay_t<std::invoke_result_t<std::decay_t<TBinder>, const TInputResponse&>>,
    typename TOutputResponse = typename MDetail::TAppFutureTraits<TNextFuture>::TResponseType>
TAppFuture<TOutputResponse> Chain(TAppFuture<TInputResponse> Future, TBinder&& Binder)
{
    if (!Future.Valid())
    {
        return MakeErrorFuture<TOutputResponse>("client_call_async_exception", "invalid_future");
    }

    MPromise<TAppResult<TOutputResponse>> Promise;
    TAppFuture<TOutputResponse> OutFuture = Promise.GetFuture();
    Future.Then(
        [Promise, Binder = std::forward<TBinder>(Binder)](TAppFuture<TInputResponse> Completed) mutable
        {
            try
            {
                const TAppResult<TInputResponse> Result = Completed.Get();
                if (!Result.IsOk())
                {
                    Promise.SetValue(MakeErrorResult<TOutputResponse>(Result.GetError()));
                    return;
                }

                TNextFuture NextFuture = std::invoke(Binder, Result.GetValue());
                if (!NextFuture.Valid())
                {
                    Promise.SetValue(MakeErrorResult<TOutputResponse>(MDetail::MakeAsyncExceptionError("invalid_chained_future")));
                    return;
                }

                NextFuture.Then(
                    [Promise](TNextFuture NextCompleted) mutable
                    {
                        try
                        {
                            Promise.SetValue(NextCompleted.Get());
                        }
                        catch (const std::exception& Ex)
                        {
                            Promise.SetValue(MakeErrorResult<TOutputResponse>(MDetail::MakeAsyncExceptionError(Ex.what())));
                        }
                        catch (...)
                        {
                            Promise.SetValue(MakeErrorResult<TOutputResponse>(MDetail::MakeAsyncExceptionError("unknown")));
                        }
                    });
            }
            catch (const std::exception& Ex)
            {
                Promise.SetValue(MakeErrorResult<TOutputResponse>(MDetail::MakeAsyncExceptionError(Ex.what())));
            }
            catch (...)
            {
                Promise.SetValue(MakeErrorResult<TOutputResponse>(MDetail::MakeAsyncExceptionError("unknown")));
            }
        });
    return OutFuture;
}

template<typename TDerived, typename TResponse>
class TClientCallAction : public MCoroutine<TAppResult<TResponse>>
{
protected:
    template<typename TStepResponse>
    void Continue(
        TAppFuture<TStepResponse> Future,
        void (TDerived::*OnSuccess)(const TStepResponse&))
    {
        if (!Future.Valid())
        {
            Fail("client_call_async_exception", "invalid_future");
            return;
        }

        Future.Then(
            [this, OnSuccess](TAppFuture<TStepResponse> Completed) mutable
            {
                try
                {
                    const TAppResult<TStepResponse> Result = Completed.Get();
                    if (!Result.IsOk())
                    {
                        Fail(Result.GetError());
                        return;
                    }

                    (static_cast<TDerived*>(this)->*OnSuccess)(Result.GetValue());
                }
                catch (const std::exception& Ex)
                {
                    Fail("client_call_async_exception", Ex.what());
                }
                catch (...)
                {
                    Fail("client_call_async_exception", "unknown");
                }
            });
    }

    void Succeed(TResponse Response)
    {
        this->Resolve(TAppResult<TResponse>::Ok(std::move(Response)));
    }

    void Fail(FAppError Error)
    {
        this->Resolve(MakeErrorResult<TResponse>(std::move(Error)));
    }

    void Fail(const char* Code, const char* Message = "")
    {
        Fail(FAppError::Make(Code ? Code : "client_call_failed", Message ? Message : ""));
    }
};

template<typename TAction, typename... TArgs>
auto StartAction(TArgs&&... Args) -> TAppFuture<typename TAction::TResponseType>
{
    return MAsync::StartCoroutine<TAction>(std::forward<TArgs>(Args)...);
}
}
