#pragma once

#include "Common/Runtime/Reflect/Reflection.h"

MSTRUCT()
struct SGetPlayerInfoRequest
{
    GENERATED_BODY()

    MPROPERTY(ValidateMeta=NotZero)
    uint64 PlayerId;
};

MSTRUCT()
struct SGetPlayerInfoResponse
{
    GENERATED_BODY()

    MPROPERTY()
    MString PlayerName;

    MPROPERTY()
    int32 Level;

    MPROPERTY()
    int32 ExperiencePoints;
};
