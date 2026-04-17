#include "Servers/Scene/Combat/MonsterManager.h"

#include "Servers/Scene/Combat/Monster.h"
#include "Servers/Scene/Combat/MonsterConfig.h"
#include "Servers/Scene/Combat/MonsterFactory.h"
#include "Servers/Scene/Combat/SceneCombatRuntime.h"

MMonsterManager::MMonsterManager()
{
    Factory = CreateDefaultSubObject<MMonsterFactory>(this, "Factory");
}

bool MMonsterManager::SpawnMonster(
    uint64 CombatEntityId,
    const FSceneCombatMonsterSpawnParams& Params,
    FCombatUnitRef& OutUnit,
    MString& OutError)
{
    if (CombatEntityId == 0)
    {
        OutError = "combat_entity_id_required";
        return false;
    }

    if (!Factory)
    {
        OutError = "monster_factory_missing";
        return false;
    }

    if (MonstersByCombatEntityId.contains(CombatEntityId))
    {
        OutError = "monster_combat_entity_exists";
        return false;
    }

    MMonster* Monster = Factory->CreateMonster(this, CombatEntityId, Params, OutError);
    if (!Monster)
    {
        if (OutError.empty())
        {
            OutError = "monster_create_failed";
        }
        return false;
    }

    MonstersByCombatEntityId[CombatEntityId] = Monster;
    IndexMonsterInScene(Monster->GetSceneId(), CombatEntityId);
    OutUnit = Monster->GetUnitRef();
    return true;
}

bool MMonsterManager::SpawnMonster(
    uint64 CombatEntityId,
    const MMonsterConfig& Config,
    FCombatUnitRef& OutUnit,
    MString& OutError)
{
    if (CombatEntityId == 0)
    {
        OutError = "combat_entity_id_required";
        return false;
    }

    if (!Factory)
    {
        OutError = "monster_factory_missing";
        return false;
    }

    if (MonstersByCombatEntityId.contains(CombatEntityId))
    {
        OutError = "monster_combat_entity_exists";
        return false;
    }

    MMonster* Monster = Factory->CreateMonster(this, CombatEntityId, Config, OutError);
    if (!Monster)
    {
        if (OutError.empty())
        {
            OutError = "monster_create_failed";
        }
        return false;
    }

    MonstersByCombatEntityId[CombatEntityId] = Monster;
    IndexMonsterInScene(Monster->GetSceneId(), CombatEntityId);
    OutUnit = Monster->GetUnitRef();
    return true;
}

bool MMonsterManager::RegisterMonsterConfig(MMonsterConfig* Config, MString& OutError)
{
    if (!Factory)
    {
        OutError = "monster_factory_missing";
        return false;
    }

    return Factory->RegisterConfig(Config, OutError);
}

bool MMonsterManager::DespawnMonster(const FCombatUnitRef& Unit, uint64* OutCombatEntityId, MString& OutError)
{
    MMonster* Monster = FindMonster(Unit);
    if (!Monster)
    {
        OutError = "scene_combat_monster_not_found";
        return false;
    }

    const uint64 CombatEntityId = Monster->GetCombatEntityId();
    if (OutCombatEntityId)
    {
        *OutCombatEntityId = CombatEntityId;
    }

    RemoveMonsterFromSceneIndex(Monster->GetSceneId(), CombatEntityId);
    MonstersByCombatEntityId.erase(CombatEntityId);
    DestroyMObject(Monster);
    return true;
}

MMonster* MMonsterManager::FindMonster(const FCombatUnitRef& Unit) const
{
    if (Unit.CombatEntityId != 0)
    {
        return FindMonsterByCombatEntityId(Unit.CombatEntityId);
    }

    return nullptr;
}

MMonster* MMonsterManager::FindMonsterByCombatEntityId(uint64 CombatEntityId) const
{
    const auto It = MonstersByCombatEntityId.find(CombatEntityId);
    return It != MonstersByCombatEntityId.end() ? It->second : nullptr;
}

TVector<FCombatUnitRef> MMonsterManager::ListMonstersInScene(uint32 SceneId) const
{
    TVector<FCombatUnitRef> Units;

    const auto SceneIt = MonsterCombatEntityIdsBySceneId.find(SceneId);
    if (SceneIt == MonsterCombatEntityIdsBySceneId.end())
    {
        return Units;
    }

    for (uint64 CombatEntityId : SceneIt->second)
    {
        if (MMonster* Monster = FindMonsterByCombatEntityId(CombatEntityId))
        {
            Units.push_back(Monster->GetUnitRef());
        }
    }

    return Units;
}

void MMonsterManager::VisitReferencedObjects(const TFunction<void(MObject*)>& Visitor) const
{
    MObject::VisitReferencedObjects(Visitor);
    if (!Visitor)
    {
        return;
    }

    Visitor(Factory);
    for (const auto& [CombatEntityId, Monster] : MonstersByCombatEntityId)
    {
        (void)CombatEntityId;
        Visitor(Monster);
    }
}

void MMonsterManager::IndexMonsterInScene(uint32 SceneId, uint64 CombatEntityId)
{
    if (SceneId == 0 || CombatEntityId == 0)
    {
        return;
    }

    MonsterCombatEntityIdsBySceneId[SceneId].insert(CombatEntityId);
}

void MMonsterManager::RemoveMonsterFromSceneIndex(uint32 SceneId, uint64 CombatEntityId)
{
    if (SceneId == 0 || CombatEntityId == 0)
    {
        return;
    }

    const auto SceneIt = MonsterCombatEntityIdsBySceneId.find(SceneId);
    if (SceneIt == MonsterCombatEntityIdsBySceneId.end())
    {
        return;
    }

    SceneIt->second.erase(CombatEntityId);
    if (SceneIt->second.empty())
    {
        MonsterCombatEntityIdsBySceneId.erase(SceneIt);
    }
}
