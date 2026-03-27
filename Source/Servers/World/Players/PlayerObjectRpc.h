#pragma once

#include "Servers/App/ObjectProxyCall.h"
#include "Servers/World/Players/PlayerProxyCall.h"

namespace MPlayerObjectRpc
{
template<typename TResponse, typename TRequest>
MFuture<TResult<TResponse, FAppError>> Call(MObject* TargetObject, const char* FunctionName, const TRequest& Request)
{
    return MObjectProxyCall::CallLocal<TResponse>(TargetObject, FunctionName, Request);
}

template<typename TResponse, typename TRequest>
MFuture<TResult<TResponse, FAppError>> Call(
    uint64 PlayerId,
    MObject* ContextObject,
    const char* FunctionName,
    const TRequest& Request,
    const char* ObjectPath = "")
{
    return MPlayerProxyCall::Bind(PlayerId, ContextObject)
        .WithPath(ObjectPath ? MString(ObjectPath) : MString())
        .Call<TResponse>(FunctionName, Request);
}

template<typename TResponse, typename TRequest>
MFuture<TResult<TResponse, FAppError>> Call(
    uint64 PlayerId,
    MObject* ContextObject,
    MPlayerProxyCall::EObjectProxyPlayerNode Node,
    const char* FunctionName,
    const TRequest& Request)
{
    return MPlayerProxyCall::Bind(PlayerId, ContextObject, Node).Call<TResponse>(FunctionName, Request);
}
}
