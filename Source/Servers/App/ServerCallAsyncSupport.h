#pragma once

#include "Common/Runtime/Object/Result.h"
#include "Servers/App/ServerRpcSupport.h"

namespace MServerCallAsyncSupport
{
template<typename TResponse>
MFuture<TResult<TResponse, FAppError>> MakeSuccessFuture(TResponse Response)
{
    return MServerRpcSupport::MakeReadyFuture(TResult<TResponse, FAppError>::Ok(std::move(Response)));
}

template<typename TResponse>
MFuture<TResult<TResponse, FAppError>> MakeErrorFuture(const char* Code, const char* Message = "")
{
    return MServerRpcSupport::MakeReadyFuture(
        MakeErrorResult<TResponse>(FAppError::Make(Code ? Code : "server_call_failed", Message ? Message : "")));
}
}
