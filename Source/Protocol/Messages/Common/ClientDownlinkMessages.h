#pragma once

#include "Common/Runtime/Reflect/Reflection.h"

MSTRUCT()
struct FClientDownlinkPushRequest
{
    MPROPERTY()
    uint64 GatewayConnectionId = 0;

    MPROPERTY()
    uint16 FunctionId = 0;

    MPROPERTY()
    TByteArray Payload;
};
