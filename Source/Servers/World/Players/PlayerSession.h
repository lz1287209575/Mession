#pragma once

#include "Common/Runtime/Object/Object.h"
#include "Common/Runtime/Reflect/Reflection.h"

MCLASS(Type=Object)
class MPlayerSession : public MObject
{
public:
    MGENERATED_BODY(MPlayerSession, MObject, 0)
public:
    MPlayerSession();

    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint64 GatewayConnectionId = 0;

    MPROPERTY()
    uint32 SessionKey = 0;

    void InitializeForLogin(uint64 InPlayerId, uint64 InGatewayConnectionId, uint32 InSessionKey);
};
