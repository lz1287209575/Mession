#pragma once

#include "Common/Runtime/Reflect/Reflection.h"

MSTRUCT()
struct FPlayerChangeGoldRequest
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    int32 DeltaGold = 0;
};

MSTRUCT()
struct FPlayerChangeGoldResponse
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint32 Gold = 0;
};

MSTRUCT()
struct FPlayerEquipItemRequest
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    MString EquippedItem;
};

MSTRUCT()
struct FPlayerEquipItemResponse
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    MString EquippedItem;
};

MSTRUCT()
struct FPlayerGrantExperienceRequest
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint32 ExperienceDelta = 0;
};

MSTRUCT()
struct FPlayerGrantExperienceResponse
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint32 Level = 0;

    MPROPERTY()
    uint32 Experience = 0;
};

MSTRUCT()
struct FPlayerModifyHealthRequest
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    int32 HealthDelta = 0;
};

MSTRUCT()
struct FPlayerModifyHealthResponse
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint32 Health = 0;
};
