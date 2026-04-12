#pragma once

#include "Common/Runtime/Concurrency/Async.h"
#include "Common/Net/Rpc/RpcServerCall.h"
#include "Common/Runtime/Object/Result.h"
#include "Servers/App/ServerRpcSupport.h"

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
template<typename TFuture>
struct TAppFutureTraits;

template<typename TResponse>
struct TAppFutureTraits<MFuture<TResult<TResponse, FAppError>>>
{
    using TResponseType = TResponse;
};

inline FAppError MakeAsyncExceptionError(const char* Message)
{
    return FAppError::Make("server_call_async_exception", Message ? Message : "unknown");
}

template<typename TResponse>
TAppFuture<TResponse> MakeExceptionFuture(const char* Message)
{
    return MServerRpcSupport::MakeReadyFuture(MakeErrorResult<TResponse>(MakeAsyncExceptionError(Message)));
}
}

template<typename TResponse>
TAppFuture<TResponse> MakeResultFuture(TAppResult<TResponse> Result)
{
    return MServerRpcSupport::MakeReadyFuture(std::move(Result));
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

    if (!Future.Valid())
    {
        return SendDeferredServerCallErrorResponse(
            Context,
            FAppError::Make("server_call_invalid_future", FunctionName ? FunctionName : "ServerCall"));
    }

    Future.Then(
        [Context](TAppFuture<TResponse> Completed) mutable
        {
            try
            {
                TAppResult<TResponse> Result = Completed.Get();
                if (Result.IsOk())
                {
                    (void)SendDeferredServerCallSuccessResponse(Context, BuildPayload(Result.GetValue()));
                    return;
                }

                (void)SendDeferredServerCallErrorResponse(Context, Result.GetError());
            }
            catch (const std::exception& Ex)
            {
                (void)SendDeferredServerCallErrorResponse(Context, FAppError::Make("server_call_exception", Ex.what()));
            }
            catch (...)
            {
                (void)SendDeferredServerCallErrorResponse(Context, FAppError::Make("server_call_exception", "unknown"));
            }
        });

    return true;
}

/** 单步映射：适合“一个异步结果 -> 一个同步响应”的短路径。 */
template<typename TInputResponse, typename TMapper, typename TOutputResponse = std::decay_t<std::invoke_result_t<std::decay_t<TMapper>, const TInputResponse&>>>
TAppFuture<TOutputResponse> Map(TAppFuture<TInputResponse> Future, TMapper&& Mapper)
{
    if (!Future.Valid())
    {
        return MDetail::MakeExceptionFuture<TOutputResponse>("invalid_future");
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

/**
 * 短链编排：适合 2 步以内的异步串联。
 * 如果流程超过 2-3 步、需要跨步骤状态，或分支很多，请改用 TServerCallAction。
 */
template<
    typename TInputResponse,
    typename TBinder,
    typename TNextFuture = std::decay_t<std::invoke_result_t<std::decay_t<TBinder>, const TInputResponse&>>,
    typename TOutputResponse = typename MDetail::TAppFutureTraits<TNextFuture>::TResponseType>
TAppFuture<TOutputResponse> Chain(TAppFuture<TInputResponse> Future, TBinder&& Binder)
{
    if (!Future.Valid())
    {
        return MDetail::MakeExceptionFuture<TOutputResponse>("invalid_future");
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

/**
 * 长流程编排：只给“3 步以上 / 需要状态 / 分支明显”的服务流程使用。
 * 目标是把业务层从匿名 Lambda 提升成具名步骤函数，而不是让每个小异步都变成 Action。
 */
template<typename TDerived, typename TResponse>
class TServerCallAction : public MCoroutine<TAppResult<TResponse>>
{
protected:
    template<typename TStepResponse>
    void Continue(
        TAppFuture<TStepResponse> Future,
        void (TDerived::*OnSuccess)(const TStepResponse&))
    {
        if (!Future.Valid())
        {
            Fail("server_call_async_exception", "invalid_future");
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
                    Fail("server_call_async_exception", Ex.what());
                }
                catch (...)
                {
                    Fail("server_call_async_exception", "unknown");
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
        Fail(FAppError::Make(Code ? Code : "server_call_failed", Message ? Message : ""));
    }
};

/** Action 启动入口：一般只在服务实现 .cpp 内部使用。 */
template<typename TAction, typename... TArgs>
auto StartAction(TArgs&&... Args) -> TAppFuture<typename TAction::TResponseType>
{
    return MAsync::StartCoroutine<TAction>(std::forward<TArgs>(Args)...);
}
}
