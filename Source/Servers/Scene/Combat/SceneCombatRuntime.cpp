#include "Servers/Scene/Combat/SceneCombatRuntime.h"

void MSceneCombatRuntime::Initialize(const MSkillCatalog* InSkillCatalog)
{
    SkillCatalog = InSkillCatalog;
}

bool MSceneCombatRuntime::SpawnAvatar(
    const FSceneCombatAvatarSnapshot& Avatar,
    uint64& OutCombatEntityId,
    MString& OutError)
{
    if (Avatar.PlayerId == 0)
    {
        OutError = "player_id_required";
        return false;
    }

    if (Avatar.SceneId == 0)
    {
        OutError = "scene_id_required";
        return false;
    }

    auto It = AvatarsByPlayerId.find(Avatar.PlayerId);
    if (It != AvatarsByPlayerId.end())
    {
        It->second.Snapshot = Avatar;
        OutCombatEntityId = It->second.CombatEntityId;
        return true;
    }

    FSceneCombatAvatarState State;
    State.CombatEntityId = NextCombatEntityId++;
    State.Snapshot = Avatar;
    OutCombatEntityId = State.CombatEntityId;
    AvatarsByPlayerId[Avatar.PlayerId] = State;
    return true;
}

bool MSceneCombatRuntime::DespawnAvatar(uint64 PlayerId, uint64* OutCombatEntityId, MString& OutError)
{
    if (PlayerId == 0)
    {
        OutError = "player_id_required";
        return false;
    }

    const auto It = AvatarsByPlayerId.find(PlayerId);
    if (It == AvatarsByPlayerId.end())
    {
        OutError = "scene_combat_avatar_not_found";
        return false;
    }

    if (OutCombatEntityId)
    {
        *OutCombatEntityId = It->second.CombatEntityId;
    }

    AvatarsByPlayerId.erase(It);
    return true;
}

bool MSceneCombatRuntime::CastSkill(
    const FSceneCastSkillRequest& Request,
    FSceneCastSkillResponse& OutResponse,
    MString& OutError)
{
    const auto CasterIt = AvatarsByPlayerId.find(Request.CasterPlayerId);
    if (CasterIt == AvatarsByPlayerId.end())
    {
        OutError = "scene_combat_caster_not_found";
        return false;
    }

    const auto TargetIt = AvatarsByPlayerId.find(Request.TargetPlayerId);
    if (TargetIt == AvatarsByPlayerId.end())
    {
        OutError = "scene_combat_target_not_found";
        return false;
    }

    FSceneCombatAvatarState& Caster = CasterIt->second;
    FSceneCombatAvatarState& Target = TargetIt->second;
    if (Caster.Snapshot.SceneId == 0 || Caster.Snapshot.SceneId != Target.Snapshot.SceneId)
    {
        OutError = "combat_scene_mismatch";
        return false;
    }

    if (!SkillCatalog)
    {
        OutError = "skill_catalog_missing";
        return false;
    }

    const FSkillSpec* SkillSpec = SkillCatalog->FindSkill(Request.SkillId);
    if (!SkillSpec)
    {
        OutError = "skill_not_found";
        return false;
    }

    if (SkillSpec->TargetType == ESkillTargetType::Self && Request.CasterPlayerId != Request.TargetPlayerId)
    {
        OutError = "skill_target_invalid";
        return false;
    }

    if (!SkillSpec->bCanTargetSelf && Request.CasterPlayerId == Request.TargetPlayerId)
    {
        OutError = "skill_target_invalid";
        return false;
    }

    FSkillExecutionContext ExecutionContext;
    ExecutionContext.CasterPlayerId = Request.CasterPlayerId;
    ExecutionContext.PrimaryTargetPlayerId = Request.TargetPlayerId;
    ExecutionContext.SceneId = Caster.Snapshot.SceneId;
    ExecutionContext.CasterSnapshot = &Caster.Snapshot;
    ExecutionContext.PrimaryTargetSnapshot = &Target.Snapshot;

    if (!SkillExecutor.ExecuteSkill(*SkillSpec, ExecutionContext, OutError))
    {
        return false;
    }

    OutResponse.bAccepted = true;
    OutResponse.SceneId = Caster.Snapshot.SceneId;
    OutResponse.CasterPlayerId = Request.CasterPlayerId;
    OutResponse.TargetPlayerId = Request.TargetPlayerId;
    OutResponse.SkillId = SkillSpec->SkillId;
    OutResponse.AppliedDamage = ExecutionContext.AppliedDamage;
    OutResponse.TargetHealth = Target.Snapshot.CurrentHealth;
    return true;
}

const FSceneCombatAvatarState* MSceneCombatRuntime::FindAvatar(uint64 PlayerId) const
{
    const auto It = AvatarsByPlayerId.find(PlayerId);
    return It != AvatarsByPlayerId.end() ? &It->second : nullptr;
}

bool MSceneCombatRuntime::ArePlayersInSameScene(uint64 PlayerA, uint64 PlayerB, uint32& OutSceneId) const
{
    const FSceneCombatAvatarState* AvatarA = FindAvatar(PlayerA);
    const FSceneCombatAvatarState* AvatarB = FindAvatar(PlayerB);
    if (!AvatarA || !AvatarB)
    {
        return false;
    }

    if (AvatarA->Snapshot.SceneId == 0 || AvatarA->Snapshot.SceneId != AvatarB->Snapshot.SceneId)
    {
        return false;
    }

    OutSceneId = AvatarA->Snapshot.SceneId;
    return true;
}

void MSceneCombatRuntime::Tick(float DeltaTime)
{
    (void)DeltaTime;
}
