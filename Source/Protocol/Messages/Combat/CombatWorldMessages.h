#pragma once

#include "Common/Runtime/Reflect/Reflection.h"
#include "Protocol/Messages/Combat/CombatTypes.h"
#include "Servers/App/ServerCallRequestValidation.h"

MSTRUCT()
struct FWorldCreateCombatAvatarRequest
{
    MPROPERTY(Meta=(NonZero, ErrorCode="player_id_required", ErrorContext="CreateCombatAvatar"))
    uint64 PlayerId = 0;

    MPROPERTY(Meta=(NonZero, ErrorCode="scene_id_required", ErrorContext="CreateCombatAvatar"))
    uint32 SceneId = 0;
};

MSTRUCT()
struct FWorldCreateCombatAvatarResponse
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint32 SceneId = 0;

    MPROPERTY()
    uint64 CombatEntityId = 0;
};

MSTRUCT()
struct FWorldCommitCombatResultRequest
{
    MPROPERTY(Meta=(NonZero, ErrorCode="player_id_required", ErrorContext="CommitCombatResult"))
    uint64 PlayerId = 0;

    MPROPERTY()
    uint32 SceneId = 0;

    MPROPERTY()
    uint32 CommittedHealth = 0;

    MPROPERTY()
    uint32 ExperienceReward = 0;
};

MSTRUCT()
struct FWorldCommitCombatResultResponse
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint32 SceneId = 0;

    MPROPERTY()
    uint32 CommittedHealth = 0;

    MPROPERTY()
    uint32 ExperienceReward = 0;
};

MSTRUCT()
struct FWorldCastSkillRequest
{
    MPROPERTY(Meta=(NonZero, ErrorCode="combat_participants_required", ErrorContext="CastSkill"))
    uint64 PlayerId = 0;

    MPROPERTY(Meta=(NonZero, ErrorCode="combat_participants_required", ErrorContext="CastSkill"))
    uint64 TargetPlayerId = 0;

    MPROPERTY(Meta=(NonZero, ErrorCode="skill_id_required", ErrorContext="CastSkill"))
    uint32 SkillId = 0;
};

MSTRUCT()
struct FWorldSpawnMonsterRequest
{
    MPROPERTY(Meta=(NonZero, ErrorCode="player_id_required", ErrorContext="SpawnMonster"))
    uint64 PlayerId = 0;

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
struct FWorldSpawnMonsterResponse
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint32 SceneId = 0;

    MPROPERTY()
    FCombatUnitRef MonsterUnit;

    MPROPERTY()
    uint32 MonsterTemplateId = 0;
};

MSTRUCT()
struct FWorldCastSkillAtUnitRequest
{
    MPROPERTY(Meta=(NonZero, ErrorCode="player_id_required", ErrorContext="CastSkillAtUnit"))
    uint64 PlayerId = 0;

    MPROPERTY()
    FCombatUnitRef TargetUnit;

    MPROPERTY(Meta=(NonZero, ErrorCode="skill_id_required", ErrorContext="CastSkillAtUnit"))
    uint32 SkillId = 0;
};

MSTRUCT()
struct FWorldCastSkillAtUnitResponse
{
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
};

MSTRUCT()
struct FWorldCastSkillResponse
{
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
};
