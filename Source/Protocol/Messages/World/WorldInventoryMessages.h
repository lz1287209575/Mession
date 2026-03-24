#pragma once

#include "Common/Runtime/Reflect/Reflection.h"

MSTRUCT()
struct SInventoryItemPayload
{
    MPROPERTY()
    uint64 InstanceId = 0;

    MPROPERTY()
    uint32 ItemId = 0;

    MPROPERTY()
    uint32 Count = 0;

    MPROPERTY()
    bool bBound = false;

    MPROPERTY()
    int64 ExpireAtUnixSeconds = 0;

    MPROPERTY()
    uint32 Flags = 0;
};
