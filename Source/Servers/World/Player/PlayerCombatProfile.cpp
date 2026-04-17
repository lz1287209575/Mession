#include "Servers/World/Player/PlayerCombatProfile.h"

#include "Servers/World/Player/Player.h"

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
    Snapshot.UnitKind = ECombatUnitKind::Player;
    Snapshot.PlayerId = PlayerId;
    Snapshot.SceneId = SceneId;
    Snapshot.MaxHealth = MaxHealth;
    Snapshot.CurrentHealth = ResolveHealthInRange(CurrentHealth, MaxHealth);
    Snapshot.AttackPower = BaseAttack;
    Snapshot.DefensePower = BaseDefense;
    Snapshot.PrimarySkillId = PrimarySkillId;
    return Snapshot;
}

uint32 MPlayerCombatProfile::RecordCommittedCombatResult(const FWorldCommitCombatResultRequest& Request)
{
    const uint32 ResolvedHealth = ResolveHealthInRange(Request.CommittedHealth, MaxHealth);
    LastResolvedSceneId = Request.SceneId;
    LastResolvedHealth = ResolvedHealth;
    MarkPropertyDirty("LastResolvedSceneId");
    MarkPropertyDirty("LastResolvedHealth");
    return ResolvedHealth;
}

MFuture<TResult<FPlayerQueryCombatProfileResponse, FAppError>> MPlayerCombatProfile::PlayerQueryCombatProfile(
    const FPlayerQueryCombatProfileRequest& /*Request*/)
{
    FPlayerQueryCombatProfileResponse Response;
    if (const MPlayer* Player = dynamic_cast<const MPlayer*>(GetOuter()))
    {
        Response.PlayerId = Player->PlayerId;
    }

    Response.BaseAttack = BaseAttack;
    Response.BaseDefense = BaseDefense;
    Response.MaxHealth = MaxHealth;
    Response.PrimarySkillId = PrimarySkillId;
    Response.LastResolvedSceneId = LastResolvedSceneId;
    Response.LastResolvedHealth = LastResolvedHealth;
    return MServerCallAsyncSupport::MakeSuccessFuture(std::move(Response));
}

MFuture<TResult<FPlayerSetPrimarySkillResponse, FAppError>> MPlayerCombatProfile::PlayerSetPrimarySkill(
    const FPlayerSetPrimarySkillRequest& Request)
{
    PrimarySkillId = Request.PrimarySkillId;
    MarkPropertyDirty("PrimarySkillId");

    FPlayerSetPrimarySkillResponse Response;
    if (const MPlayer* Player = dynamic_cast<const MPlayer*>(GetOuter()))
    {
        Response.PlayerId = Player->PlayerId;
    }
    Response.PrimarySkillId = PrimarySkillId;
    return MServerCallAsyncSupport::MakeSuccessFuture(std::move(Response));
}
