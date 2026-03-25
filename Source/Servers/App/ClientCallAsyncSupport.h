#pragma once

#include "Common/Net/Rpc/RpcClientCall.h"
#include "Common/Runtime/Concurrency/Promise.h"
#include "Common/Runtime/Object/Result.h"

namespace MClientCallAsyncSupport
{
template<typename TResponse, typename TErrorMapper>
bool StartDeferred(
    const SClientCallContext& Context,
    MFuture<TResult<TResponse, FAppError>> Future,
    TErrorMapper&& ErrorMapper)
{
    if (!Context.IsValid())
    {
        return false;
    }

    RegisterDeferredClientCall(Context);
    Future.Then(
        [Context, ErrorMapper = std::forward<TErrorMapper>(ErrorMapper)](MFuture<TResult<TResponse, FAppError>> Completed) mutable
        {
            try
            {
                const TResult<TResponse, FAppError> Result = Completed.Get();
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
bool StartDeferred(const SClientCallContext& Context, MFuture<TResult<TResponse, FAppError>> Future)
{
    return StartDeferred<TResponse>(
        Context,
        std::move(Future),
        [](const FAppError&)
        {
            return TResponse {};
        });
}
}
