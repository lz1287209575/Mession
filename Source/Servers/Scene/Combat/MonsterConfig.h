#pragma once

#include "Common/Runtime/Object/Object.h"
#include "Common/Runtime/Reflect/Reflection.h"
#include "Protocol/Messages/Combat/CombatSceneMessages.h"

MCLASS(Type=Object)
class MMonsterConfig : public MObject
{
public:
    MGENERATED_BODY(MMonsterConfig, MObject, 0)
public:
    uint32 GetMonsterTemplateId() const;
    const MString& GetDebugName() const;
    const FSceneCombatMonsterSpawnParams& GetSpawnParams() const;
    const TVector<uint32>& GetSkillIds() const;

    void SetMonsterTemplateId(uint32 InMonsterTemplateId);
    void SetDebugName(const MString& InDebugName);
    void SetSpawnParams(const FSceneCombatMonsterSpawnParams& InSpawnParams);
    void SetSkillIds(const TVector<uint32>& InSkillIds);

    FSceneCombatMonsterSpawnParams BuildSpawnParams() const;

private:
    MPROPERTY(Edit | Asset)
    uint32 MonsterTemplateId = 0;

    MPROPERTY(Edit | Asset)
    MString DebugName;

    MPROPERTY(Edit | Asset)
    FSceneCombatMonsterSpawnParams SpawnParams;

    MPROPERTY(Edit | Asset)
    TVector<uint32> SkillIds;
};
