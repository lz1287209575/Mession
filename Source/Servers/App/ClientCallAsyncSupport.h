#pragma once

#include "Common/Net/ServerRpcRuntime.h"
#include "Common/Runtime/Object/Result.h"

namespace MClientCallAsyncSupport
{
template<typename TResponse, typename TErrorMapper>
bool StartDeferred(
    const SGeneratedClientCallContext& Context,
    MFuture<TResult<TResponse, FAppError>> Future,
    TErrorMapper&& ErrorMapper)
{
    if (!Context.IsValid())
    {
        return false;
    }

    MarkCurrentClientCallDeferred();
    Future.Then(
        [Context, ErrorMapper = std::forward<TErrorMapper>(ErrorMapper)](MFuture<TResult<TResponse, FAppError>> Completed) mutable
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
        });
    return true;
}

template<typename TResponse>
bool StartDeferred(const SGeneratedClientCallContext& Context, MFuture<TResult<TResponse, FAppError>> Future)
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
