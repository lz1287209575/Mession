#pragma once

#include "Common/Runtime/Reflect/Reflection.h"
#include "Protocol/Messages/Combat/CombatTypes.h"

MSTRUCT()
struct FSceneCombatAvatarSnapshot
{
    MPROPERTY()
    ECombatUnitKind UnitKind = ECombatUnitKind::Player;

    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint32 SceneId = 0;

    MPROPERTY()
    uint32 CurrentHealth = 0;

    MPROPERTY()
    uint32 MaxHealth = 0;

    MPROPERTY()
    uint32 AttackPower = 0;

    MPROPERTY()
    uint32 DefensePower = 0;

    MPROPERTY()
    uint32 PrimarySkillId = 0;
};

MSTRUCT()
struct FSceneSpawnCombatAvatarRequest
{
    MPROPERTY()
    FSceneCombatAvatarSnapshot Avatar;
};

MSTRUCT()
struct FSceneCombatMonsterSpawnParams
{
    MPROPERTY()
    uint32 SceneId = 0;

    MPROPERTY()
    uint32 MonsterTemplateId = 0;

    MPROPERTY()
    MString DebugName;

    MPROPERTY()
    uint32 CurrentHealth = 0;

    MPROPERTY()
    uint32 MaxHealth = 0;

    MPROPERTY()
    uint32 AttackPower = 0;

    MPROPERTY()
    uint32 DefensePower = 0;

    MPROPERTY()
    uint32 PrimarySkillId = 1001;

    MPROPERTY()
    uint32 ExperienceReward = 0;

    MPROPERTY()
    uint32 GoldReward = 0;
};

MSTRUCT()
struct FSceneSpawnMonsterRequest
{
    MPROPERTY()
    uint32 SceneId = 0;

    MPROPERTY()
    uint32 MonsterTemplateId = 0;

    MPROPERTY()
    MString DebugName;

    MPROPERTY()
    uint32 CurrentHealth = 0;

    MPROPERTY()
    uint32 MaxHealth = 0;

    MPROPERTY()
    uint32 AttackPower = 0;

    MPROPERTY()
    uint32 DefensePower = 0;

    MPROPERTY()
    uint32 PrimarySkillId = 1001;

    MPROPERTY()
    uint32 ExperienceReward = 0;

    MPROPERTY()
    uint32 GoldReward = 0;
};

MSTRUCT()
struct FSceneSpawnCombatAvatarResponse
{
    MPROPERTY()
    FCombatUnitRef Unit;

    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint32 SceneId = 0;

    MPROPERTY()
    uint64 CombatEntityId = 0;
};

MSTRUCT()
struct FSceneSpawnMonsterResponse
{
    MPROPERTY()
    FCombatUnitRef Unit;

    MPROPERTY()
    uint32 SceneId = 0;

    MPROPERTY()
    uint32 MonsterTemplateId = 0;

    MPROPERTY()
    uint64 CombatEntityId = 0;
};

MSTRUCT()
struct FSceneDespawnCombatUnitRequest
{
    MPROPERTY()
    FCombatUnitRef Unit;

    MPROPERTY()
    MString Reason;
};

MSTRUCT()
struct FSceneDespawnCombatUnitResponse
{
    MPROPERTY()
    FCombatUnitRef Unit;

    MPROPERTY()
    uint64 CombatEntityId = 0;
};

MSTRUCT()
struct FSceneDespawnCombatAvatarRequest
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    MString Reason;
};

MSTRUCT()
struct FSceneDespawnCombatAvatarResponse
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint64 CombatEntityId = 0;
};

MSTRUCT()
struct FSceneCastSkillRequest
{
    MPROPERTY()
    FCombatUnitRef CasterUnit;

    MPROPERTY()
    FCombatUnitRef TargetUnit;

    MPROPERTY()
    uint64 CasterPlayerId = 0;

    MPROPERTY()
    uint64 TargetPlayerId = 0;

    MPROPERTY()
    uint32 SkillId = 0;
};

MSTRUCT()
struct FSceneCastSkillResponse
{
    MPROPERTY()
    bool bAccepted = false;

    MPROPERTY()
    uint32 SceneId = 0;

    MPROPERTY()
    FCombatUnitRef CasterUnit;

    MPROPERTY()
    FCombatUnitRef TargetUnit;

    MPROPERTY()
    uint64 CasterPlayerId = 0;

    MPROPERTY()
    uint64 TargetPlayerId = 0;

    MPROPERTY()
    uint32 SkillId = 0;

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
