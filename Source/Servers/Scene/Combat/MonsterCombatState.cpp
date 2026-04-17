#include "Servers/Scene/Combat/MonsterCombatState.h"

void MMonsterCombatState::Initialize(
    uint32 InSceneId,
    uint32 InMonsterTemplateId,
    uint32 InCurrentHealth,
    const MString& InDebugName)
{
    SceneId = InSceneId;
    MonsterTemplateId = InMonsterTemplateId;
    CurrentHealth = InCurrentHealth;
    DebugName = InDebugName;

    Snapshot = FSceneCombatAvatarSnapshot {};
    Snapshot.UnitKind = ECombatUnitKind::Monster;
    Snapshot.SceneId = SceneId;
    Snapshot.CurrentHealth = CurrentHealth;
}

void MMonsterCombatState::RefreshSnapshot(
    uint32 MaxHealth,
    uint32 AttackPower,
    uint32 DefensePower,
    uint32 PrimarySkillId)
{
    Snapshot.UnitKind = ECombatUnitKind::Monster;
    Snapshot.PlayerId = 0;
    Snapshot.SceneId = SceneId;
    Snapshot.MaxHealth = MaxHealth;
    Snapshot.CurrentHealth = CurrentHealth > MaxHealth ? MaxHealth : CurrentHealth;
    Snapshot.AttackPower = AttackPower;
    Snapshot.DefensePower = DefensePower;
    Snapshot.PrimarySkillId = PrimarySkillId;
}

FSceneCombatAvatarSnapshot* MMonsterCombatState::GetMutableSnapshot()
{
    return &Snapshot;
}

const FSceneCombatAvatarSnapshot& MMonsterCombatState::GetSnapshot() const
{
    return Snapshot;
}

void MMonsterCombatState::SyncFromSnapshot()
{
    SceneId = Snapshot.SceneId;
    CurrentHealth = Snapshot.CurrentHealth;
}
