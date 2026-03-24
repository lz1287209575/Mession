#pragma once

#include "Common/Runtime/Concurrency/Promise.h"
#include "Common/Runtime/Object/Result.h"
#include "Protocol/Messages/Common/AppMessages.h"

template<typename TResponse>
inline TResult<TResponse, FAppError> MakeRpcErrorResult(const char* Code, const char* Message = "")
{
    return MakeErrorResult<TResponse>(
        FAppError::Make(Code ? Code : "rpc_error", Message ? Message : ""));
}

template<typename TResponse>
inline MFuture<TResult<TResponse, FAppError>> MakeRpcErrorFuture(const char* Code, const char* Message = "")
{
    MPromise<TResult<TResponse, FAppError>> Promise;
    MFuture<TResult<TResponse, FAppError>> Future = Promise.GetFuture();
    Promise.SetValue(MakeRpcErrorResult<TResponse>(Code, Message));
    return Future;
}
