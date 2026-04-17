#pragma once

#include "Common/Runtime/Object/Object.h"
#include "Common/Runtime/Reflect/Reflection.h"

class MMonster;
class MMonsterConfig;
struct FSceneCombatMonsterSpawnParams;

MCLASS(Type=Object)
class MMonsterFactory : public MObject
{
public:
    MGENERATED_BODY(MMonsterFactory, MObject, 0)
public:
    bool RegisterConfig(MMonsterConfig* Config, MString& OutError);
    const MMonsterConfig* FindConfig(uint32 MonsterTemplateId) const;

    MMonster* CreateMonster(
        MObject* Owner,
        uint64 CombatEntityId,
        const FSceneCombatMonsterSpawnParams& Params,
        MString& OutError) const;

    MMonster* CreateMonster(
        MObject* Owner,
        uint64 CombatEntityId,
        const MMonsterConfig& Config,
        MString& OutError) const;

    void VisitReferencedObjects(const TFunction<void(MObject*)>& Visitor) const override;

private:
    void ApplyConfigOverrides(
        const FSceneCombatMonsterSpawnParams& Overrides,
        FSceneCombatMonsterSpawnParams& InOutParams) const;

    bool ResolveSpawnParams(
        const FSceneCombatMonsterSpawnParams& Params,
        FSceneCombatMonsterSpawnParams& OutResolvedParams) const;

    TMap<uint32, MMonsterConfig*> ConfigsByMonsterTemplateId;
};
