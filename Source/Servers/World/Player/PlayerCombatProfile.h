#pragma once

#include "Common/Runtime/Object/Object.h"
#include "Common/Runtime/Reflect/Reflection.h"
#include "Protocol/Messages/Combat/CombatSceneMessages.h"
#include "Protocol/Messages/Combat/CombatWorldMessages.h"

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

    MPROPERTY(PersistentData)
    uint32 LastResolvedSceneId = 0;

    MPROPERTY(PersistentData)
    uint32 LastResolvedHealth = 100;

    void InitializeDefaults();

    FSceneCombatAvatarSnapshot BuildSceneAvatarSnapshot(
        uint64 PlayerId,
        uint32 SceneId,
        uint32 CurrentHealth) const;

    void ApplyCommittedCombatResult(const FWorldCommitCombatResultRequest& Request);
};
