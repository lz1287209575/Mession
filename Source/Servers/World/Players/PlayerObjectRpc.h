#pragma once

#include "Servers/App/ObjectProxyCall.h"

namespace MPlayerObjectRpc
{
template<typename TResponse, typename TRequest>
MFuture<TResult<TResponse, FAppError>> Call(MObject* TargetObject, const char* FunctionName, const TRequest& Request)
{
    return MObjectProxyCall::CallLocal<TResponse>(TargetObject, FunctionName, Request);
}
}
