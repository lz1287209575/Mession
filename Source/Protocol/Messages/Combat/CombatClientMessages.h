#pragma once

#include "Common/Runtime/Reflect/Reflection.h"
#include "Protocol/Messages/Combat/CombatWorldMessages.h"

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

MSTRUCT()
struct FClientDebugSpawnMonsterResponse
{
    MPROPERTY()
    bool bSuccess = false;

    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint32 SceneId = 0;

    MPROPERTY()
    FCombatUnitRef MonsterUnit;

    MPROPERTY()
    uint32 MonsterTemplateId = 0;

    MPROPERTY()
    MString Error;
};

MSTRUCT()
struct FClientCastSkillAtUnitResponse
{
    MPROPERTY()
    bool bSuccess = false;

    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    FCombatUnitRef TargetUnit;

    MPROPERTY()
    uint32 SkillId = 0;

    MPROPERTY()
    uint32 SceneId = 0;

    MPROPERTY()
    uint32 AppliedDamage = 0;

    MPROPERTY()
    uint32 TargetHealth = 0;

    MPROPERTY()
    bool bTargetDefeated = false;

    MPROPERTY()
    uint32 ExperienceReward = 0;

    MPROPERTY()
    uint32 GoldReward = 0;

    MPROPERTY()
    MString Error;
};

MSTRUCT()
struct FClientSetPrimarySkillResponse
{
    MPROPERTY()
    bool bSuccess = false;

    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint32 PrimarySkillId = 0;

    MPROPERTY()
    MString Error;
};
