#pragma once

#include "Common/Runtime/Object/Object.h"
#include "Common/Runtime/Reflect/Reflection.h"
#include "Protocol/Messages/Combat/CombatSceneMessages.h"
#include "Protocol/Messages/Combat/CombatWorldMessages.h"
#include "Servers/App/ServerCallAsyncSupport.h"
#include "Servers/App/ServerCallRequestValidation.h"

MSTRUCT()
struct FPlayerQueryCombatProfileRequest
{
    MPROPERTY(Meta=(NonZero, ErrorCode="player_id_required", ErrorContext="PlayerQueryCombatProfile"))
    uint64 PlayerId = 0;
};

MSTRUCT()
struct FPlayerQueryCombatProfileResponse
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint32 BaseAttack = 0;

    MPROPERTY()
    uint32 BaseDefense = 0;

    MPROPERTY()
    uint32 MaxHealth = 0;

    MPROPERTY()
    uint32 PrimarySkillId = 0;

    MPROPERTY()
    uint32 LastResolvedSceneId = 0;

    MPROPERTY()
    uint32 LastResolvedHealth = 0;
};

MSTRUCT()
struct FPlayerSetPrimarySkillRequest
{
    MPROPERTY(Meta=(NonZero, ErrorCode="player_id_required", ErrorContext="PlayerSetPrimarySkill"))
    uint64 PlayerId = 0;

    MPROPERTY(Meta=(NonZero, ErrorCode="primary_skill_id_required", ErrorContext="PlayerSetPrimarySkill"))
    uint32 PrimarySkillId = 0;
};

MSTRUCT()
struct FPlayerSetPrimarySkillResponse
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint32 PrimarySkillId = 0;
};

MCLASS(Type=Object)
class MPlayerCombatProfile : public MObject
{
public:
    MGENERATED_BODY(MPlayerCombatProfile, MObject, 0)
public:
    MPlayerCombatProfile();

    MPROPERTY(PersistentData | Replicated)
    uint32 BaseAttack = 10;

    MPROPERTY(PersistentData | Replicated)
    uint32 BaseDefense = 5;

    MPROPERTY(PersistentData | Replicated)
    uint32 MaxHealth = 100;

    MPROPERTY(PersistentData | Replicated)
    uint32 PrimarySkillId = 1001;

    // Snapshot-only fields for audit/replay/diagnostics. They are not used as
    // authoritative gameplay state.
    MPROPERTY(PersistentData)
    uint32 LastResolvedSceneId = 0;

    // Snapshot-only fields for audit/replay/diagnostics. They are not used as
    // authoritative gameplay state.
    MPROPERTY(PersistentData)
    uint32 LastResolvedHealth = 100;

    void InitializeDefaults();

    FSceneCombatAvatarSnapshot BuildSceneAvatarSnapshot(
        uint64 PlayerId,
        uint32 SceneId,
        uint32 CurrentHealth) const;

    uint32 RecordCommittedCombatResult(const FWorldCommitCombatResultRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FPlayerQueryCombatProfileResponse, FAppError>> PlayerQueryCombatProfile(
        const FPlayerQueryCombatProfileRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FPlayerSetPrimarySkillResponse, FAppError>> PlayerSetPrimarySkill(
        const FPlayerSetPrimarySkillRequest& Request);
};
