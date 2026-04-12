#include "Servers/World/Player/PlayerService.h"

#include "Protocol/Messages/Combat/CombatSceneMessages.h"
#include "Servers/World/Player/Player.h"

MFuture<TResult<FWorldCreateCombatAvatarResponse, FAppError>> MPlayerService::CreateCombatAvatar(
    const FWorldCreateCombatAvatarRequest& Request)
{
    if (Request.PlayerId == 0)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FWorldCreateCombatAvatarResponse>(
            "player_id_required",
            "CreateCombatAvatar");
    }

    if (Request.SceneId == 0)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FWorldCreateCombatAvatarResponse>(
            "scene_id_required",
            "CreateCombatAvatar");
    }

    if (!WorldServer->GetScene() || !WorldServer->GetScene()->IsAvailable())
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FWorldCreateCombatAvatarResponse>(
            "scene_server_unavailable",
            "CreateCombatAvatar");
    }

    const MPlayer* Player = FindPlayer(Request.PlayerId);
    if (!Player)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FWorldCreateCombatAvatarResponse>(
            "player_not_found",
            "CreateCombatAvatar");
    }

    FSceneCombatAvatarSnapshot AvatarSnapshot;
    if (!Player->TryBuildCombatAvatarSnapshot(Request.SceneId, AvatarSnapshot))
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FWorldCreateCombatAvatarResponse>(
            "player_combat_profile_missing",
            "CreateCombatAvatar");
    }

    FSceneSpawnCombatAvatarRequest SceneRequest;
    SceneRequest.Avatar = std::move(AvatarSnapshot);

    return MServerCallAsyncSupport::Map(
        WorldServer->GetScene()->SpawnCombatAvatar(SceneRequest),
        [](const FSceneSpawnCombatAvatarResponse& Value)
        {
            FWorldCreateCombatAvatarResponse Response;
            Response.PlayerId = Value.PlayerId;
            Response.SceneId = Value.SceneId;
            Response.CombatEntityId = Value.CombatEntityId;
            return Response;
        });
}

MFuture<TResult<FWorldCommitCombatResultResponse, FAppError>> MPlayerService::CommitCombatResult(
    const FWorldCommitCombatResultRequest& Request)
{
    if (Request.PlayerId == 0)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FWorldCommitCombatResultResponse>(
            "player_id_required",
            "CommitCombatResult");
    }

    MPlayer* Player = FindPlayer(Request.PlayerId);
    if (!Player)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FWorldCommitCombatResultResponse>(
            "player_not_found",
            "CommitCombatResult");
    }

    const uint32 ResolvedHealth = Player->ApplyCombatResult(Request);

    FWorldCommitCombatResultResponse Response;
    Response.PlayerId = Request.PlayerId;
    Response.SceneId = Request.SceneId;
    Response.CommittedHealth = ResolvedHealth;
    Response.ExperienceReward = Request.ExperienceReward;

    MFuture<TResult<FWorldCommitCombatResultResponse, FAppError>> Future =
        MServerCallAsyncSupport::MakeSuccessFuture(std::move(Response));
    Future.Then([this, PlayerId = Request.PlayerId](MFuture<TResult<FWorldCommitCombatResultResponse, FAppError>> Completed)
    {
        try
        {
            const TResult<FWorldCommitCombatResultResponse, FAppError> Result = Completed.Get();
            if (Result.IsOk())
            {
                QueueScenePlayerUpdateNotify(PlayerId);
            }
        }
        catch (...)
        {
        }
    });
    return Future;
}

MFuture<TResult<FWorldCastSkillResponse, FAppError>> MPlayerService::CastSkill(
    const FWorldCastSkillRequest& Request)
{
    if (Request.PlayerId == 0 || Request.TargetPlayerId == 0)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FWorldCastSkillResponse>(
            "combat_participants_required",
            "CastSkill");
    }

    if (Request.PlayerId == Request.TargetPlayerId)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FWorldCastSkillResponse>(
            "combat_target_invalid",
            "CastSkill");
    }

    if (Request.SkillId == 0)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FWorldCastSkillResponse>(
            "skill_id_required",
            "CastSkill");
    }

    if (!WorldServer->GetScene() || !WorldServer->GetScene()->IsAvailable())
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FWorldCastSkillResponse>(
            "scene_server_unavailable",
            "CastSkill");
    }

    MPlayer* Caster = FindPlayer(Request.PlayerId);
    MPlayer* Target = FindPlayer(Request.TargetPlayerId);
    if (!Caster || !Target)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FWorldCastSkillResponse>(
            "player_not_found",
            "CastSkill");
    }

    const uint32 SceneId = Caster->ResolveCurrentSceneId();
    if (SceneId == 0 || SceneId != Target->ResolveCurrentSceneId())
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FWorldCastSkillResponse>(
            "combat_scene_mismatch",
            "CastSkill");
    }

    return MServerCallAsyncSupport::Chain(
        EnsureCombatAvatar(Request.PlayerId, SceneId),
        [this, Request, SceneId](const FWorldCreateCombatAvatarResponse&)
        {
            return MServerCallAsyncSupport::Chain(
                EnsureCombatAvatar(Request.TargetPlayerId, SceneId),
                [this, Request](const FWorldCreateCombatAvatarResponse&)
                {
                    FSceneCastSkillRequest SceneRequest;
                    SceneRequest.CasterPlayerId = Request.PlayerId;
                    SceneRequest.TargetPlayerId = Request.TargetPlayerId;
                    SceneRequest.SkillId = Request.SkillId;

                    return MServerCallAsyncSupport::Chain(
                        WorldServer->GetScene()->CastSkill(SceneRequest),
                        [this, Request](const FSceneCastSkillResponse& SceneResponse)
                        {
                            FWorldCommitCombatResultRequest CommitRequest;
                            CommitRequest.PlayerId = SceneResponse.TargetPlayerId;
                            CommitRequest.SceneId = SceneResponse.SceneId;
                            CommitRequest.CommittedHealth = SceneResponse.TargetHealth;

                            return MServerCallAsyncSupport::Map(
                                CommitCombatResult(CommitRequest),
                                [Request, SceneResponse](const FWorldCommitCombatResultResponse& CommitResponse)
                                {
                                    FWorldCastSkillResponse Response;
                                    Response.PlayerId = Request.PlayerId;
                                    Response.TargetPlayerId = Request.TargetPlayerId;
                                    Response.SkillId = Request.SkillId;
                                    Response.SceneId = SceneResponse.SceneId;
                                    Response.AppliedDamage = SceneResponse.AppliedDamage;
                                    Response.TargetHealth = CommitResponse.CommittedHealth;
                                    return Response;
                                });
                        });
                });
        });
}

MFuture<TResult<FWorldCreateCombatAvatarResponse, FAppError>> MPlayerService::EnsureCombatAvatar(
    uint64 PlayerId,
    uint32 SceneId)
{
    FWorldCreateCombatAvatarRequest Request;
    Request.PlayerId = PlayerId;
    Request.SceneId = SceneId;
    return CreateCombatAvatar(Request);
}

