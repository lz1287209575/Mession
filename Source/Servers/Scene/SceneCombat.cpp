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
    Response.PlayerId = Request.Avatar.PlayerId;
    Response.SceneId = Request.Avatar.SceneId;
    Response.CombatEntityId = CombatEntityId;
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

MFuture<TResult<FSceneCastSkillResponse, FAppError>> MSceneCombat::CastSkill(
    const FSceneCastSkillRequest& Request)
{
    if (!Runtime)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FSceneCastSkillResponse>(
            "scene_combat_service_not_initialized",
            "CastSkill");
    }

    if (Request.CasterPlayerId == 0 || Request.TargetPlayerId == 0)
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
