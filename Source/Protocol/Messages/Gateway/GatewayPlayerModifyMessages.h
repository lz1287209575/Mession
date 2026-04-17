#pragma once

#include "Common/Runtime/Reflect/Reflection.h"
#include "Protocol/Messages/World/PlayerModifyMessages.h"

MSTRUCT()
struct FClientChangeGoldResponse
{
    MPROPERTY()
    bool bSuccess = false;

    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint32 Gold = 0;

    MPROPERTY()
    MString Error;
};

MSTRUCT()
struct FClientEquipItemResponse
{
    MPROPERTY()
    bool bSuccess = false;

    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    MString EquippedItem;

    MPROPERTY()
    MString Error;
};

MSTRUCT()
struct FClientGrantExperienceResponse
{
    MPROPERTY()
    bool bSuccess = false;

    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint32 Level = 0;

    MPROPERTY()
    uint32 Experience = 0;

    MPROPERTY()
    MString Error;
};

MSTRUCT()
struct FClientModifyHealthResponse
{
    MPROPERTY()
    bool bSuccess = false;

    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint32 Health = 0;

    MPROPERTY()
    MString Error;
};
