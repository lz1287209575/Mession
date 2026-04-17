#pragma once

#include "Common/Runtime/Object/Object.h"
#include "Common/Runtime/Reflect/Reflection.h"
#include "Protocol/Messages/Combat/CombatTypes.h"

class MMonster;
class MMonsterConfig;
class MMonsterFactory;
struct FSceneCombatMonsterSpawnParams;

MCLASS(Type=Object)
class MMonsterManager : public MObject
{
public:
    MGENERATED_BODY(MMonsterManager, MObject, 0)
public:
    MMonsterManager();

    bool SpawnMonster(
        uint64 CombatEntityId,
        const FSceneCombatMonsterSpawnParams& Params,
        FCombatUnitRef& OutUnit,
        MString& OutError);

    bool SpawnMonster(
        uint64 CombatEntityId,
        const MMonsterConfig& Config,
        FCombatUnitRef& OutUnit,
        MString& OutError);

    bool RegisterMonsterConfig(MMonsterConfig* Config, MString& OutError);

    bool DespawnMonster(const FCombatUnitRef& Unit, uint64* OutCombatEntityId, MString& OutError);

    MMonster* FindMonster(const FCombatUnitRef& Unit) const;
    MMonster* FindMonsterByCombatEntityId(uint64 CombatEntityId) const;
    TVector<FCombatUnitRef> ListMonstersInScene(uint32 SceneId) const;

    void VisitReferencedObjects(const TFunction<void(MObject*)>& Visitor) const override;

private:
    void IndexMonsterInScene(uint32 SceneId, uint64 CombatEntityId);
    void RemoveMonsterFromSceneIndex(uint32 SceneId, uint64 CombatEntityId);

    MMonsterFactory* Factory = nullptr;
    TMap<uint64, MMonster*> MonstersByCombatEntityId;
    TMap<uint32, TSet<uint64>> MonsterCombatEntityIdsBySceneId;
};
