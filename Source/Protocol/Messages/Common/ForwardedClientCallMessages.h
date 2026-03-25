#pragma once

#include "Common/Runtime/Reflect/Reflection.h"

MSTRUCT()
struct FForwardedClientCallRequest
{
    MPROPERTY()
    uint64 GatewayConnectionId = 0;

    MPROPERTY()
    uint16 ClientFunctionId = 0;

    MPROPERTY()
    uint64 ClientCallId = 0;

    MPROPERTY()
    TByteArray Payload;
};

MSTRUCT()
struct FForwardedClientCallResponse
{
    MPROPERTY()
    TByteArray Payload;
};
