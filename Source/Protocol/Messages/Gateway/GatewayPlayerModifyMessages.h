#pragma once

#include "Common/Runtime/Reflect/Reflection.h"

MSTRUCT()
struct FClientChangeGoldRequest
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    int32 DeltaGold = 0;
};

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
struct FClientEquipItemRequest
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    MString EquippedItem;
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
struct FClientGrantExperienceRequest
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint32 ExperienceDelta = 0;
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
struct FClientModifyHealthRequest
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    int32 HealthDelta = 0;
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
