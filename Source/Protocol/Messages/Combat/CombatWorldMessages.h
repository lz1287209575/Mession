#pragma once

#include "Common/Runtime/Reflect/Reflection.h"

MSTRUCT()
struct FWorldCreateCombatAvatarRequest
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
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
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint64 TargetPlayerId = 0;

    MPROPERTY()
    uint32 SkillId = 0;
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
