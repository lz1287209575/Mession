#pragma once

#include "Common/Runtime/Reflect/Reflection.h"
#include "Servers/App/ServerCallRequestValidation.h"

MSTRUCT()
struct FPlayerChangeGoldRequest
{
    MPROPERTY(Meta=(NonZero, ErrorCode="player_id_required", ErrorContext="PlayerChangeGold"))
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
    MPROPERTY(Meta=(NonZero, ErrorCode="player_id_required", ErrorContext="PlayerEquipItem"))
    uint64 PlayerId = 0;

    MPROPERTY(Meta=(NonEmpty, ErrorCode="equipped_item_required", ErrorContext="PlayerEquipItem"))
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
    MPROPERTY(Meta=(NonZero, ErrorCode="player_id_required", ErrorContext="PlayerGrantExperience"))
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
    MPROPERTY(Meta=(NonZero, ErrorCode="player_id_required", ErrorContext="PlayerModifyHealth"))
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
