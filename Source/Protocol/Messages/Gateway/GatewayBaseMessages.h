#pragma once

#include "Common/Runtime/Reflect/Reflection.h"

MSTRUCT()
struct FClientEchoRequest
{
    MPROPERTY()
    MString Message;
};

MSTRUCT()
struct FClientEchoResponse
{
    MPROPERTY()
    uint64 ConnectionId = 0;

    MPROPERTY()
    MString Message;
};

MSTRUCT()
struct FClientHeartbeatRequest
{
    MPROPERTY()
    uint32 Sequence = 0;
};

MSTRUCT()
struct FClientHeartbeatResponse
{
    MPROPERTY()
    bool bSuccess = false;

    MPROPERTY()
    uint32 Sequence = 0;

    MPROPERTY()
    uint64 ConnectionId = 0;

    MPROPERTY()
    MString Error;
};
