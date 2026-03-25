#pragma once

#include "Common/Runtime/Reflect/Reflection.h"
#include "Protocol/Messages/Common/ObjectProxyMessages.h"
#include "Servers/App/ObjectProxyRegistry.h"
#include "Servers/App/ServerCallAsyncSupport.h"

MCLASS(Type=Service)
class MObjectProxyServiceEndpoint : public MObject
{
public:
    MGENERATED_BODY(MObjectProxyServiceEndpoint, MObject, 0)
public:
    void Initialize(MObjectProxyRegistry* InRegistry);

    MFUNCTION(ServerCall)
    MFuture<TResult<FObjectProxyInvokeResponse, FAppError>> InvokeObjectCall(
        const FObjectProxyInvokeRequest& Request);

private:
    MObjectProxyRegistry* Registry = nullptr;
};
