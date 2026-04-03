#include "Servers/World/Services/WorldCombatServiceEndpoint.h"

#include "Protocol/Messages/Combat/CombatSceneMessages.h"
#include "Servers/World/Players/Player.h"

void MWorldCombatServiceEndpoint::Initialize(
    TMap<uint64, MPlayer*>* InOnlinePlayers,
    MWorldSceneRpc* InSceneRpc)
{
    OnlinePlayers = InOnlinePlayers;
    SceneRpc = InSceneRpc;
}

MFuture<TResult<FWorldCreateCombatAvatarResponse, FAppError>> MWorldCombatServiceEndpoint::CreateCombatAvatar(
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

    if (!OnlinePlayers)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FWorldCreateCombatAvatarResponse>(
            "world_online_players_missing",
            "CreateCombatAvatar");
    }

    if (!SceneRpc)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FWorldCreateCombatAvatarResponse>(
            "world_scene_rpc_missing",
            "CreateCombatAvatar");
    }

    const MPlayer* Player = FindPlayer(Request.PlayerId);
    if (!Player)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FWorldCreateCombatAvatarResponse>(
            "player_not_found",
            "CreateCombatAvatar");
    }

    const MPlayerCombatProfile* CombatProfile = Player->GetCombatProfile();
    if (!CombatProfile)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FWorldCreateCombatAvatarResponse>(
            "player_combat_profile_missing",
            "CreateCombatAvatar");
    }

    FSceneSpawnCombatAvatarRequest SceneRequest;
    SceneRequest.Avatar = CombatProfile->BuildSceneAvatarSnapshot(
        Player->PlayerId,
        Request.SceneId,
        Player->ResolveCurrentHealth());

    return MServerCallAsyncSupport::Map(
        SceneRpc->SpawnCombatAvatar(SceneRequest),
        [](const FSceneSpawnCombatAvatarResponse& Value)
        {
            FWorldCreateCombatAvatarResponse Response;
            Response.PlayerId = Value.PlayerId;
            Response.SceneId = Value.SceneId;
            Response.CombatEntityId = Value.CombatEntityId;
            return Response;
        });
}

MFuture<TResult<FWorldCommitCombatResultResponse, FAppError>> MWorldCombatServiceEndpoint::CommitCombatResult(
    const FWorldCommitCombatResultRequest& Request)
{
    if (Request.PlayerId == 0)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FWorldCommitCombatResultResponse>(
            "player_id_required",
            "CommitCombatResult");
    }

    if (!OnlinePlayers)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FWorldCommitCombatResultResponse>(
            "world_online_players_missing",
            "CommitCombatResult");
    }

    MPlayer* Player = FindPlayer(Request.PlayerId);
    if (!Player)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FWorldCommitCombatResultResponse>(
            "player_not_found",
            "CommitCombatResult");
    }

    uint32 ResolvedHealth = Request.CommittedHealth;
    if (MPlayerCombatProfile* CombatProfile = Player->GetCombatProfile())
    {
        CombatProfile->ApplyCommittedCombatResult(Request);
        ResolvedHealth = CombatProfile->LastResolvedHealth;
    }

    if (MPlayerProfile* Profile = Player->GetProfile())
    {
        if (MPlayerProgression* Progression = Profile->GetProgression())
        {
            Progression->SetHealth(ResolvedHealth);
        }
    }

    if (MPlayerPawn* Pawn = Player->GetPawn())
    {
        Pawn->SetHealth(ResolvedHealth);
    }

    FWorldCommitCombatResultResponse Response;
    Response.PlayerId = Request.PlayerId;
    Response.SceneId = Request.SceneId;
    Response.CommittedHealth = ResolvedHealth;
    Response.ExperienceReward = Request.ExperienceReward;
    return MServerCallAsyncSupport::MakeSuccessFuture(std::move(Response));
}

MFuture<TResult<FWorldCastSkillResponse, FAppError>> MWorldCombatServiceEndpoint::CastSkill(
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

    if (!OnlinePlayers)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FWorldCastSkillResponse>(
            "world_online_players_missing",
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
                        SceneRpc->CastSkill(SceneRequest),
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

MPlayer* MWorldCombatServiceEndpoint::FindPlayer(uint64 PlayerId) const
{
    if (!OnlinePlayers)
    {
        return nullptr;
    }

    const auto It = OnlinePlayers->find(PlayerId);
    return It != OnlinePlayers->end() ? It->second : nullptr;
}

MFuture<TResult<FWorldCreateCombatAvatarResponse, FAppError>> MWorldCombatServiceEndpoint::EnsureCombatAvatar(
    uint64 PlayerId,
    uint32 SceneId)
{
    FWorldCreateCombatAvatarRequest Request;
    Request.PlayerId = PlayerId;
    Request.SceneId = SceneId;
    return CreateCombatAvatar(Request);
}
