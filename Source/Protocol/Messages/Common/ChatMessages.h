#pragma once

#include "Common/Runtime/Reflect/Reflection.h"

MSTRUCT()
struct SChatMessage
{
    MPROPERTY()
    uint64 FromPlayerId = 0;

    MPROPERTY()
    MString Message;
};

MSTRUCT()
struct SChatBroadcastPayload
{
    MPROPERTY()
    MString Message;
};
