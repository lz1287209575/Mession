#include "Servers/Scene/SceneCombat.h"

#include "Servers/Scene/Combat/SceneCombatRuntime.h"

void MSceneCombat::Initialize(
    TMap<uint64, uint32>* InPlayerScenes,
    MSceneCombatRuntime* InRuntime)
{
    PlayerScenes = InPlayerScenes;
    Runtime = InRuntime;
}

MFuture<TResult<FSceneSpawnCombatAvatarResponse, FAppError>> MSceneCombat::SpawnCombatAvatar(
    const FSceneSpawnCombatAvatarRequest& Request)
{
    if (!PlayerScenes || !Runtime)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FSceneSpawnCombatAvatarResponse>(
            "scene_combat_service_not_initialized",
            "SpawnCombatAvatar");
    }

    if (Request.Avatar.PlayerId == 0)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FSceneSpawnCombatAvatarResponse>(
            "player_id_required",
            "SpawnCombatAvatar");
    }

    if (Request.Avatar.SceneId == 0)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FSceneSpawnCombatAvatarResponse>(
            "scene_id_required",
            "SpawnCombatAvatar");
    }

    const auto SceneIt = PlayerScenes->find(Request.Avatar.PlayerId);
    if (SceneIt == PlayerScenes->end() || SceneIt->second != Request.Avatar.SceneId)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FSceneSpawnCombatAvatarResponse>(
            "player_scene_membership_missing",
            "SpawnCombatAvatar");
    }

    uint64 CombatEntityId = 0;
    MString RuntimeError;
    if (!Runtime->SpawnAvatar(Request.Avatar, CombatEntityId, RuntimeError))
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FSceneSpawnCombatAvatarResponse>(
            RuntimeError.c_str(),
            "SpawnCombatAvatar");
    }

    FSceneSpawnCombatAvatarResponse Response;
    Response.Unit = FCombatUnitRef::MakePlayer(CombatEntityId, Request.Avatar.PlayerId);
    Response.PlayerId = Request.Avatar.PlayerId;
    Response.SceneId = Request.Avatar.SceneId;
    Response.CombatEntityId = CombatEntityId;
    return MServerCallAsyncSupport::MakeSuccessFuture(std::move(Response));
}

MFuture<TResult<FSceneSpawnMonsterResponse, FAppError>> MSceneCombat::SpawnMonster(
    const FSceneSpawnMonsterRequest& Request)
{
    if (!Runtime)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FSceneSpawnMonsterResponse>(
            "scene_combat_service_not_initialized",
            "SpawnMonster");
    }

    FSceneCombatMonsterSpawnParams Params;
    Params.SceneId = Request.SceneId;
    Params.MonsterTemplateId = Request.MonsterTemplateId;
    Params.DebugName = Request.DebugName;
    Params.CurrentHealth = Request.CurrentHealth;
    Params.MaxHealth = Request.MaxHealth;
    Params.AttackPower = Request.AttackPower;
    Params.DefensePower = Request.DefensePower;
    Params.PrimarySkillId = Request.PrimarySkillId;
    Params.ExperienceReward = Request.ExperienceReward;
    Params.GoldReward = Request.GoldReward;

    FCombatUnitRef Unit;
    MString RuntimeError;
    if (!Runtime->SpawnMonster(Params, Unit, RuntimeError))
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FSceneSpawnMonsterResponse>(
            RuntimeError.c_str(),
            "SpawnMonster");
    }

    FSceneSpawnMonsterResponse Response;
    Response.Unit = Unit;
    Response.SceneId = Params.SceneId;
    Response.MonsterTemplateId = Params.MonsterTemplateId;
    Response.CombatEntityId = Unit.CombatEntityId;
    return MServerCallAsyncSupport::MakeSuccessFuture(std::move(Response));
}

MFuture<TResult<FSceneDespawnCombatAvatarResponse, FAppError>> MSceneCombat::DespawnCombatAvatar(
    const FSceneDespawnCombatAvatarRequest& Request)
{
    if (!Runtime)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FSceneDespawnCombatAvatarResponse>(
            "scene_combat_service_not_initialized",
            "DespawnCombatAvatar");
    }

    uint64 CombatEntityId = 0;
    MString RuntimeError;
    if (!Runtime->DespawnAvatar(Request.PlayerId, &CombatEntityId, RuntimeError))
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FSceneDespawnCombatAvatarResponse>(
            RuntimeError.c_str(),
            "DespawnCombatAvatar");
    }

    FSceneDespawnCombatAvatarResponse Response;
    Response.PlayerId = Request.PlayerId;
    Response.CombatEntityId = CombatEntityId;
    return MServerCallAsyncSupport::MakeSuccessFuture(std::move(Response));
}

MFuture<TResult<FSceneDespawnCombatUnitResponse, FAppError>> MSceneCombat::DespawnCombatUnit(
    const FSceneDespawnCombatUnitRequest& Request)
{
    if (!Runtime)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FSceneDespawnCombatUnitResponse>(
            "scene_combat_service_not_initialized",
            "DespawnCombatUnit");
    }

    if (!Request.Unit.IsValid())
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FSceneDespawnCombatUnitResponse>(
            "combat_target_invalid",
            "DespawnCombatUnit");
    }

    uint64 CombatEntityId = 0;
    MString RuntimeError;
    if (!Runtime->DespawnUnit(Request.Unit, &CombatEntityId, RuntimeError))
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FSceneDespawnCombatUnitResponse>(
            RuntimeError.c_str(),
            "DespawnCombatUnit");
    }

    FSceneDespawnCombatUnitResponse Response;
    Response.Unit = Request.Unit;
    Response.CombatEntityId = CombatEntityId;
    return MServerCallAsyncSupport::MakeSuccessFuture(std::move(Response));
}

MFuture<TResult<FSceneCastSkillResponse, FAppError>> MSceneCombat::CastSkill(
    const FSceneCastSkillRequest& Request)
{
    if (!Runtime)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FSceneCastSkillResponse>(
            "scene_combat_service_not_initialized",
            "CastSkill");
    }

    const bool bHasNewUnitRefs = Request.CasterUnit.IsValid() && Request.TargetUnit.IsValid();
    const bool bHasLegacyPlayerIds = Request.CasterPlayerId != 0 && Request.TargetPlayerId != 0;
    if (!bHasNewUnitRefs && !bHasLegacyPlayerIds)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FSceneCastSkillResponse>(
            "combat_participants_required",
            "CastSkill");
    }

    if (Request.SkillId == 0)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FSceneCastSkillResponse>(
            "skill_id_required",
            "CastSkill");
    }

    FSceneCastSkillResponse Response;
    MString RuntimeError;
    if (!Runtime->CastSkill(Request, Response, RuntimeError))
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FSceneCastSkillResponse>(
            RuntimeError.c_str(),
            "CastSkill");
    }

    return MServerCallAsyncSupport::MakeSuccessFuture(std::move(Response));
}
