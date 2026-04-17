#pragma once

#include "Common/Runtime/Object/Object.h"
#include "Common/Runtime/Reflect/Reflection.h"
#include "Protocol/Messages/Combat/CombatSceneMessages.h"

MCLASS(Type=Object)
class MMonsterCombatState : public MObject
{
public:
    MGENERATED_BODY(MMonsterCombatState, MObject, 0)
public:
    MPROPERTY()
    uint32 SceneId = 0;

    MPROPERTY()
    uint32 MonsterTemplateId = 0;

    MPROPERTY()
    uint32 CurrentHealth = 0;

    MPROPERTY()
    MString DebugName;

    void Initialize(
        uint32 InSceneId,
        uint32 InMonsterTemplateId,
        uint32 InCurrentHealth,
        const MString& InDebugName);

    void RefreshSnapshot(
        uint32 MaxHealth,
        uint32 AttackPower,
        uint32 DefensePower,
        uint32 PrimarySkillId);

    FSceneCombatAvatarSnapshot* GetMutableSnapshot();
    const FSceneCombatAvatarSnapshot& GetSnapshot() const;
    void SyncFromSnapshot();

private:
    FSceneCombatAvatarSnapshot Snapshot;
};
