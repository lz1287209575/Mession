#pragma once

#include "Common/Runtime/Object/Object.h"
#include "Common/Runtime/Reflect/Reflection.h"
#include "Protocol/Messages/Combat/CombatSceneMessages.h"
#include "Protocol/Messages/Combat/CombatTypes.h"
#include "Servers/Scene/Combat/MonsterCombatProfile.h"
#include "Servers/Scene/Combat/MonsterCombatState.h"

struct FSceneCombatMonsterSpawnParams;

MCLASS(Type=Object)
class MMonster : public MObject
{
public:
    MGENERATED_BODY(MMonster, MObject, 0)
public:
    MMonster();

    void InitializeForSpawn(
        uint64 InCombatEntityId,
        const FSceneCombatMonsterSpawnParams& Params);

    uint64 GetCombatEntityId() const;
    FCombatUnitRef GetUnitRef() const;
    uint32 GetSceneId() const;
    uint32 GetMonsterTemplateId() const;
    const MString& GetDebugName() const;
    uint32 GetExperienceReward() const;
    uint32 GetGoldReward() const;

    const FSceneCombatAvatarSnapshot& GetCombatSnapshot() const;
    FSceneCombatAvatarSnapshot* GetMutableCombatSnapshot();
    void SyncCombatStateFromSnapshot();

    MMonsterCombatProfile* GetCombatProfile() const;
    MMonsterCombatState* GetCombatState() const;

    void VisitReferencedObjects(const TFunction<void(MObject*)>& Visitor) const override;

private:
    MPROPERTY()
    uint64 CombatEntityId = 0;

    MMonsterCombatProfile* CombatProfile = nullptr;
    MMonsterCombatState* CombatState = nullptr;
};
