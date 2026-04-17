#pragma once

#include "Common/Runtime/MLib.h"
#include "Protocol/Messages/Combat/CombatSceneMessages.h"
#include "Servers/Scene/Combat/SkillExecutionContext.h"
#include "Servers/Scene/Combat/SkillCatalog.h"
#include "Servers/Scene/Combat/SkillStepExecutor.h"

class MMonster;
class MMonsterConfig;
class MMonsterManager;

struct FSceneCombatAvatarState
{
    FCombatUnitRef Unit;
    uint64 CombatEntityId = 0;
    FSceneCombatAvatarSnapshot Snapshot;
};

class MSceneCombatRuntime
{
public:
    void Initialize(const MSkillCatalog* InSkillCatalog, MMonsterManager* InMonsterManager);

    bool SpawnAvatar(const FSceneCombatAvatarSnapshot& Avatar, uint64& OutCombatEntityId, MString& OutError);
    bool SpawnMonster(const FSceneCombatMonsterSpawnParams& Params, FCombatUnitRef& OutUnit, MString& OutError);
    bool SpawnMonster(const MMonsterConfig& Config, FCombatUnitRef& OutUnit, MString& OutError);
    bool DespawnAvatar(uint64 PlayerId, uint64* OutCombatEntityId, MString& OutError);
    bool DespawnUnit(const FCombatUnitRef& Unit, uint64* OutCombatEntityId, MString& OutError);
    bool CastSkill(const FSceneCastSkillRequest& Request, FSceneCastSkillResponse& OutResponse, MString& OutError);

    const FSceneCombatAvatarState* FindAvatar(uint64 PlayerId) const;
    const FSceneCombatAvatarState* FindCombatUnit(const FCombatUnitRef& Unit) const;
    TVector<FCombatUnitRef> ListUnitsInScene(uint32 SceneId, ECombatUnitKind FilterKind = ECombatUnitKind::Unknown) const;
    bool ArePlayersInSameScene(uint64 PlayerA, uint64 PlayerB, uint32& OutSceneId) const;

    void Tick(float DeltaTime);

private:
    struct FResolvedCombatUnit
    {
        FCombatUnitRef Unit;
        uint64 CombatEntityId = 0;
        const FSceneCombatAvatarSnapshot* Snapshot = nullptr;
        FSceneCombatAvatarSnapshot* MutableSnapshot = nullptr;
        MMonster* Monster = nullptr;
    };

    const FSceneCombatAvatarState* FindUnit(const FCombatUnitRef& Unit) const;
    bool ResolveUnitHandle(const FCombatUnitRef& Unit, FResolvedCombatUnit& OutUnit) const;
    FCombatUnitRef ResolveLegacyPlayerUnit(uint64 PlayerId) const;
    void IndexUnitInScene(uint32 SceneId, uint64 CombatEntityId);
    void RemoveUnitFromSceneIndex(uint32 SceneId, uint64 CombatEntityId);

    const MSkillCatalog* SkillCatalog = nullptr;
    MMonsterManager* MonsterManager = nullptr;
    MSkillStepExecutor SkillExecutor;
    uint64 NextCombatEntityId = 1;
    TMap<uint64, FSceneCombatAvatarState> AvatarsByCombatEntityId;
    TMap<uint64, uint64> CombatEntityIdByPlayerId;
    TMap<uint32, TSet<uint64>> PlayerCombatEntityIdsBySceneId;
};
