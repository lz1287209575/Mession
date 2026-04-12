#pragma once

#include "Common/Runtime/Reflect/Reflection.h"
#include "Protocol/Messages/Common/ObjectCallMessages.h"
#include "Servers/App/ObjectCallRegistry.h"
#include "Servers/App/ServerCallAsyncSupport.h"

MCLASS(Type=Service)
class MObjectCallRouter : public MObject
{
public:
    MGENERATED_BODY(MObjectCallRouter, MObject, 0)
public:
    void Initialize(MObjectCallRegistry* InRegistry);

    MFUNCTION(ServerCall)
    MFuture<TResult<FObjectCallResponse, FAppError>> DispatchObjectCall(
        const FObjectCallRequest& Request);

private:
    MObjectCallRegistry* Registry = nullptr;
};

