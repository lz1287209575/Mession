#pragma once

#include "Common/Runtime/Reflect/Reflection.h"

MSTRUCT()
struct FClientCastSkillRequest
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint64 TargetPlayerId = 0;

    MPROPERTY()
    uint32 SkillId = 0;
};

MSTRUCT()
struct FClientCastSkillResponse
{
    MPROPERTY()
    bool bSuccess = false;

    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint64 TargetPlayerId = 0;

    MPROPERTY()
    uint32 SkillId = 0;

    MPROPERTY()
    uint32 SceneId = 0;

    MPROPERTY()
    uint32 AppliedDamage = 0;

    MPROPERTY()
    uint32 TargetHealth = 0;

    MPROPERTY()
    MString Error;
};
