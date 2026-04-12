#include "Servers/World/Player/PlayerCombatProfile.h"

namespace
{
uint32 ResolveHealthInRange(uint32 RequestedHealth, uint32 MaxHealth)
{
    if (MaxHealth == 0)
    {
        return 0;
    }

    return RequestedHealth > MaxHealth ? MaxHealth : RequestedHealth;
}
}

MPlayerCombatProfile::MPlayerCombatProfile()
{
    InitializeDefaults();
}

void MPlayerCombatProfile::InitializeDefaults()
{
    if (MaxHealth == 0)
    {
        MaxHealth = 100;
    }

    if (LastResolvedHealth == 0)
    {
        LastResolvedHealth = MaxHealth;
    }
}

FSceneCombatAvatarSnapshot MPlayerCombatProfile::BuildSceneAvatarSnapshot(
    uint64 PlayerId,
    uint32 SceneId,
    uint32 CurrentHealth) const
{
    FSceneCombatAvatarSnapshot Snapshot;
    Snapshot.PlayerId = PlayerId;
    Snapshot.SceneId = SceneId;
    Snapshot.MaxHealth = MaxHealth;
    Snapshot.CurrentHealth = ResolveHealthInRange(CurrentHealth, MaxHealth);
    Snapshot.AttackPower = BaseAttack;
    Snapshot.DefensePower = BaseDefense;
    Snapshot.PrimarySkillId = PrimarySkillId;
    return Snapshot;
}

void MPlayerCombatProfile::ApplyCommittedCombatResult(const FWorldCommitCombatResultRequest& Request)
{
    LastResolvedSceneId = Request.SceneId;
    LastResolvedHealth = ResolveHealthInRange(Request.CommittedHealth, MaxHealth);
    MarkPropertyDirty("LastResolvedSceneId");
    MarkPropertyDirty("LastResolvedHealth");
}
