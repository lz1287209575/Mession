#include "Servers/World/Player/PlayerService.h"

#include "Common/Runtime/Concurrency/FiberAwait.h"
#include "Protocol/Messages/Combat/CombatSceneMessages.h"
#include "Servers/World/Player/Player.h"
#include "Servers/World/Player/PlayerProfile.h"
#include "Servers/World/Player/PlayerProgression.h"

#include <limits>

namespace
{
template<typename TResponse>
TResult<TResponse, FAppError> MakeCombatError(const char* Code, const char* Message = "")
{
    return MakeErrorResult<TResponse>(FAppError::Make(
        Code ? Code : "combat_command_failed",
        Message ? Message : ""));
}

uint32 ResolveMonsterExperienceReward(const FWorldSpawnMonsterRequest& Request, uint32 ResolvedMaxHealth)
{
    if (Request.ExperienceReward != 0)
    {
        return Request.ExperienceReward;
    }

    return ResolvedMaxHealth;
}

uint32 ResolveMonsterGoldReward(const FWorldSpawnMonsterRequest& Request, uint32 ResolvedAttackPower)
{
    if (Request.GoldReward != 0)
    {
        return Request.GoldReward;
    }

    return ResolvedAttackPower != 0 ? ResolvedAttackPower : 1;
}
}

MFuture<TResult<FWorldCreateCombatAvatarResponse, FAppError>> MPlayerService::CreateCombatAvatar(
    const FWorldCreateCombatAvatarRequest& Request)
{
    if (const TOptional<FAppError> ValidationError = MServerCallRequestValidation::ValidateRequest(Request);
        ValidationError.has_value())
    {
        return MServerCallAsyncSupport::MakeResultFuture(MakeErrorResult<FWorldCreateCombatAvatarResponse>(*ValidationError));
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

    return MServerCallAsyncSupport::StartFiber<FWorldCreateCombatAvatarResponse>(
        WorldServer->GetTaskRunner(),
        [this, SceneRequest = std::move(SceneRequest)]() mutable
        {
            const FSceneSpawnCombatAvatarResponse Value =
                MAwaitOk(WorldServer->GetScene()->SpawnCombatAvatar(SceneRequest));
            FWorldCreateCombatAvatarResponse Response;
            Response.PlayerId = Value.PlayerId;
            Response.SceneId = Value.SceneId;
            Response.CombatEntityId = Value.CombatEntityId;
            return TResult<FWorldCreateCombatAvatarResponse, FAppError>::Ok(std::move(Response));
        });
}

MFuture<TResult<FWorldCommitCombatResultResponse, FAppError>> MPlayerService::CommitCombatResult(
    const FWorldCommitCombatResultRequest& Request)
{
    return DispatchRuntimeCommand<FWorldCommitCombatResultResponse>(
        Request,
        "CommitCombatResult",
        {},
        &MPlayerService::DoCommitCombatResult);
}

MFuture<TResult<FWorldCastSkillResponse, FAppError>> MPlayerService::CastSkill(
    const FWorldCastSkillRequest& Request)
{
    if (const TOptional<FAppError> ValidationError = MServerCallRequestValidation::ValidateRequest(Request);
        ValidationError.has_value())
    {
        return MServerCallAsyncSupport::MakeResultFuture(MakeErrorResult<FWorldCastSkillResponse>(*ValidationError));
    }

    if (Request.PlayerId == Request.TargetPlayerId)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FWorldCastSkillResponse>(
            "combat_target_invalid",
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

    TVector<SPlayerCommandParticipant> Participants;
    Participants.push_back(SPlayerCommandParticipant{Request.PlayerId, 0, true});
    Participants.push_back(SPlayerCommandParticipant{Request.TargetPlayerId, 0, true});
    return DispatchRuntimeCommandMany<FWorldCastSkillResponse>(
        Request,
        std::move(Participants),
        "CastSkill",
        {},
        &MPlayerService::DoCastSkill);
}

MFuture<TResult<FWorldSpawnMonsterResponse, FAppError>> MPlayerService::SpawnMonster(
    const FWorldSpawnMonsterRequest& Request)
{
    if (const TOptional<FAppError> ValidationError = MServerCallRequestValidation::ValidateRequest(Request);
        ValidationError.has_value())
    {
        return MServerCallAsyncSupport::MakeResultFuture(MakeErrorResult<FWorldSpawnMonsterResponse>(*ValidationError));
    }

    if (!WorldServer->GetScene() || !WorldServer->GetScene()->IsAvailable())
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FWorldSpawnMonsterResponse>(
            "scene_server_unavailable",
            "SpawnMonster");
    }

    if (!FindPlayer(Request.PlayerId))
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FWorldSpawnMonsterResponse>(
            "player_not_found",
            "SpawnMonster");
    }

    return DispatchRuntimeCommand<FWorldSpawnMonsterResponse>(
        Request,
        "SpawnMonster",
        {},
        &MPlayerService::DoSpawnMonster);
}

MFuture<TResult<FWorldCastSkillAtUnitResponse, FAppError>> MPlayerService::CastSkillAtUnit(
    const FWorldCastSkillAtUnitRequest& Request)
{
    if (const TOptional<FAppError> ValidationError = MServerCallRequestValidation::ValidateRequest(Request);
        ValidationError.has_value())
    {
        return MServerCallAsyncSupport::MakeResultFuture(MakeErrorResult<FWorldCastSkillAtUnitResponse>(*ValidationError));
    }

    if (!Request.TargetUnit.IsValid())
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FWorldCastSkillAtUnitResponse>(
            "combat_target_invalid",
            "CastSkillAtUnit");
    }

    if (!WorldServer->GetScene() || !WorldServer->GetScene()->IsAvailable())
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FWorldCastSkillAtUnitResponse>(
            "scene_server_unavailable",
            "CastSkillAtUnit");
    }

    if (!FindPlayer(Request.PlayerId))
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FWorldCastSkillAtUnitResponse>(
            "player_not_found",
            "CastSkillAtUnit");
    }

    TVector<SPlayerCommandParticipant> Participants;
    Participants.push_back(SPlayerCommandParticipant{Request.PlayerId, 0, true});
    if (Request.TargetUnit.IsPlayer() && Request.TargetUnit.PlayerId != 0 && Request.TargetUnit.PlayerId != Request.PlayerId)
    {
        Participants.push_back(SPlayerCommandParticipant{Request.TargetUnit.PlayerId, 0, true});
    }

    return DispatchRuntimeCommandMany<FWorldCastSkillAtUnitResponse>(
        Request,
        std::move(Participants),
        "CastSkillAtUnit",
        {},
        &MPlayerService::DoCastSkillAtUnit);
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

TResult<FWorldCommitCombatResultResponse, FAppError> MPlayerService::CommitCombatResultImmediate(
    const FWorldCommitCombatResultRequest& Request)
{
    MPlayer* Player = FindPlayer(Request.PlayerId);
    if (!Player)
    {
        return MakeCombatError<FWorldCommitCombatResultResponse>("player_not_found", "CommitCombatResult");
    }

    const uint32 ResolvedHealth = Player->ApplyCombatResult(Request);

    if (Request.ExperienceReward != 0)
    {
        MPlayerProfile* Profile = Player->GetProfile();
        MPlayerProgression* Progression = Profile ? Profile->GetProgression() : nullptr;
        if (!Progression)
        {
            return MakeCombatError<FWorldCommitCombatResultResponse>(
                "player_progression_missing",
                "CommitCombatResult");
        }

        const TResult<FPlayerGrantExperienceResponse, FAppError> ExperienceResult =
            Progression->ApplyExperienceDelta(Request.ExperienceReward);
        if (ExperienceResult.IsErr())
        {
            return MakeErrorResult<FWorldCommitCombatResultResponse>(ExperienceResult.GetError());
        }
    }

    FWorldCommitCombatResultResponse Response;
    Response.PlayerId = Request.PlayerId;
    Response.SceneId = Request.SceneId;
    Response.CommittedHealth = ResolvedHealth;
    Response.ExperienceReward = Request.ExperienceReward;

    QueueScenePlayerUpdateNotify(Request.PlayerId);
    return TResult<FWorldCommitCombatResultResponse, FAppError>::Ok(std::move(Response));
}

TResult<FWorldCommitCombatResultResponse, FAppError> MPlayerService::DoCommitCombatResult(
    FWorldCommitCombatResultRequest Request)
{
    return CommitCombatResultImmediate(Request);
}

TResult<FWorldCastSkillResponse, FAppError> MPlayerService::DoCastSkill(FWorldCastSkillRequest Request)
{
    FWorldCastSkillAtUnitRequest UnitRequest;
    UnitRequest.PlayerId = Request.PlayerId;
    UnitRequest.TargetUnit = FCombatUnitRef::MakePlayer(0, Request.TargetPlayerId);
    UnitRequest.SkillId = Request.SkillId;

    const TResult<FWorldCastSkillAtUnitResponse, FAppError> UnitResult = DoCastSkillAtUnit(UnitRequest);
    if (UnitResult.IsErr())
    {
        return MakeErrorResult<FWorldCastSkillResponse>(UnitResult.GetError());
    }

    const FWorldCastSkillAtUnitResponse& UnitResponse = UnitResult.GetValue();

    FWorldCastSkillResponse Response;
    Response.PlayerId = Request.PlayerId;
    Response.TargetPlayerId = Request.TargetPlayerId;
    Response.SkillId = Request.SkillId;
    Response.SceneId = UnitResponse.SceneId;
    Response.AppliedDamage = UnitResponse.AppliedDamage;
    Response.TargetHealth = UnitResponse.TargetHealth;
    return TResult<FWorldCastSkillResponse, FAppError>::Ok(std::move(Response));
}

TResult<FWorldSpawnMonsterResponse, FAppError> MPlayerService::DoSpawnMonster(FWorldSpawnMonsterRequest Request)
{
    MPlayer* Player = FindPlayer(Request.PlayerId);
    if (!Player)
    {
        return MakeCombatError<FWorldSpawnMonsterResponse>("player_not_found", "SpawnMonster");
    }

    const uint32 SceneId = Player->ResolveCurrentSceneId();
    if (SceneId == 0)
    {
        return MakeCombatError<FWorldSpawnMonsterResponse>("combat_scene_mismatch", "SpawnMonster");
    }

    FSceneSpawnMonsterRequest SceneRequest;
    SceneRequest.SceneId = SceneId;
    SceneRequest.MonsterTemplateId = Request.MonsterTemplateId != 0 ? Request.MonsterTemplateId : 9001;
    SceneRequest.DebugName = Request.DebugName;
    SceneRequest.MaxHealth = Request.MaxHealth != 0 ? Request.MaxHealth : 50;
    SceneRequest.CurrentHealth = Request.CurrentHealth != 0 ? Request.CurrentHealth : SceneRequest.MaxHealth;
    SceneRequest.AttackPower = Request.AttackPower != 0 ? Request.AttackPower : 3;
    SceneRequest.DefensePower = Request.DefensePower;
    SceneRequest.PrimarySkillId = Request.PrimarySkillId != 0 ? Request.PrimarySkillId : 1001;
    SceneRequest.ExperienceReward = ResolveMonsterExperienceReward(Request, SceneRequest.MaxHealth);
    SceneRequest.GoldReward = ResolveMonsterGoldReward(Request, SceneRequest.AttackPower);

    const FSceneSpawnMonsterResponse SceneResponse =
        MAwaitOk(WorldServer->GetScene()->SpawnMonster(SceneRequest));

    FWorldSpawnMonsterResponse Response;
    Response.PlayerId = Request.PlayerId;
    Response.SceneId = SceneResponse.SceneId;
    Response.MonsterUnit = SceneResponse.Unit;
    Response.MonsterTemplateId = SceneResponse.MonsterTemplateId;
    return TResult<FWorldSpawnMonsterResponse, FAppError>::Ok(std::move(Response));
}

TResult<FWorldCastSkillAtUnitResponse, FAppError> MPlayerService::DoCastSkillAtUnit(FWorldCastSkillAtUnitRequest Request)
{
    if (!Request.TargetUnit.IsValid())
    {
        return MakeCombatError<FWorldCastSkillAtUnitResponse>("combat_target_invalid", "CastSkillAtUnit");
    }

    MPlayer* Caster = FindPlayer(Request.PlayerId);
    if (!Caster)
    {
        return MakeCombatError<FWorldCastSkillAtUnitResponse>("player_not_found", "CastSkillAtUnit");
    }

    const uint32 SceneId = Caster->ResolveCurrentSceneId();
    if (SceneId == 0)
    {
        return MakeCombatError<FWorldCastSkillAtUnitResponse>("combat_scene_mismatch", "CastSkillAtUnit");
    }

    const FWorldCreateCombatAvatarResponse CasterAvatar =
        MAwaitOk(EnsureCombatAvatar(Request.PlayerId, SceneId));

    FCombatUnitRef TargetUnit = Request.TargetUnit;
    if (TargetUnit.IsPlayer())
    {
        if (TargetUnit.PlayerId == 0)
        {
            return MakeCombatError<FWorldCastSkillAtUnitResponse>("combat_target_invalid", "CastSkillAtUnit");
        }

        MPlayer* TargetPlayer = FindPlayer(TargetUnit.PlayerId);
        if (!TargetPlayer)
        {
            return MakeCombatError<FWorldCastSkillAtUnitResponse>("player_not_found", "CastSkillAtUnit");
        }

        if (TargetPlayer->ResolveCurrentSceneId() != SceneId)
        {
            return MakeCombatError<FWorldCastSkillAtUnitResponse>("combat_scene_mismatch", "CastSkillAtUnit");
        }

        const FWorldCreateCombatAvatarResponse TargetAvatar =
            MAwaitOk(EnsureCombatAvatar(TargetUnit.PlayerId, SceneId));
        TargetUnit = FCombatUnitRef::MakePlayer(TargetAvatar.CombatEntityId, TargetUnit.PlayerId);
    }
    else if (TargetUnit.IsMonster())
    {
        if (TargetUnit.CombatEntityId == 0)
        {
            return MakeCombatError<FWorldCastSkillAtUnitResponse>("combat_target_invalid", "CastSkillAtUnit");
        }
    }

    FSceneCastSkillRequest SceneRequest;
    SceneRequest.CasterUnit = FCombatUnitRef::MakePlayer(CasterAvatar.CombatEntityId, Request.PlayerId);
    SceneRequest.TargetUnit = TargetUnit;
    SceneRequest.CasterPlayerId = Request.PlayerId;
    SceneRequest.TargetPlayerId = TargetUnit.PlayerId;
    SceneRequest.SkillId = Request.SkillId;

    const FSceneCastSkillResponse SceneResponse =
        MAwaitOk(WorldServer->GetScene()->CastSkill(SceneRequest));

    uint32 ResolvedTargetHealth = SceneResponse.TargetHealth;
    uint32 ExperienceReward = 0;
    uint32 GoldReward = 0;
    if (SceneResponse.TargetPlayerId != 0)
    {
        FWorldCommitCombatResultRequest CommitRequest;
        CommitRequest.PlayerId = SceneResponse.TargetPlayerId;
        CommitRequest.SceneId = SceneResponse.SceneId;
        CommitRequest.CommittedHealth = SceneResponse.TargetHealth;

        const TResult<FWorldCommitCombatResultResponse, FAppError> CommitResult =
            CommitCombatResultImmediate(CommitRequest);
        if (CommitResult.IsErr())
        {
            return MakeErrorResult<FWorldCastSkillAtUnitResponse>(CommitResult.GetError());
        }

        ResolvedTargetHealth = CommitResult.GetValue().CommittedHealth;
    }
    else if (SceneResponse.TargetUnit.IsMonster() && SceneResponse.bTargetDefeated)
    {
        FSceneDespawnCombatUnitRequest DespawnRequest;
        DespawnRequest.Unit = SceneResponse.TargetUnit;
        DespawnRequest.Reason = "monster_defeated";
        (void)MAwaitOk(WorldServer->GetScene()->DespawnCombatUnit(DespawnRequest));

        ExperienceReward = SceneResponse.ExperienceReward;
        GoldReward = SceneResponse.GoldReward;

        MPlayerProfile* Profile = Caster->GetProfile();
        MPlayerProgression* Progression = Profile ? Profile->GetProgression() : nullptr;
        MPlayerInventory* Inventory = Profile ? Profile->GetInventory() : nullptr;
        if (!Progression || !Inventory)
        {
            return MakeCombatError<FWorldCastSkillAtUnitResponse>(
                "player_reward_state_missing",
                "CastSkillAtUnit");
        }

        if (ExperienceReward != 0)
        {
            const TResult<FPlayerGrantExperienceResponse, FAppError> ExperienceResult =
                Progression->ApplyExperienceDelta(ExperienceReward);
            if (ExperienceResult.IsErr())
            {
                return MakeErrorResult<FWorldCastSkillAtUnitResponse>(ExperienceResult.GetError());
            }
        }

        if (GoldReward != 0)
        {
            if (GoldReward > static_cast<uint32>(std::numeric_limits<int32>::max()))
            {
                return MakeCombatError<FWorldCastSkillAtUnitResponse>(
                    "player_gold_overflow",
                    "CastSkillAtUnit");
            }

            const TResult<FPlayerChangeGoldResponse, FAppError> GoldResult =
                Inventory->ApplyGoldDelta(static_cast<int32>(GoldReward));
            if (GoldResult.IsErr())
            {
                return MakeErrorResult<FWorldCastSkillAtUnitResponse>(GoldResult.GetError());
            }
        }
    }

    FWorldCastSkillAtUnitResponse Response;
    Response.PlayerId = Request.PlayerId;
    Response.TargetUnit = SceneResponse.TargetUnit;
    Response.SkillId = Request.SkillId;
    Response.SceneId = SceneResponse.SceneId;
    Response.AppliedDamage = SceneResponse.AppliedDamage;
    Response.TargetHealth = ResolvedTargetHealth;
    Response.bTargetDefeated = SceneResponse.bTargetDefeated;
    Response.ExperienceReward = ExperienceReward;
    Response.GoldReward = GoldReward;
    return TResult<FWorldCastSkillAtUnitResponse, FAppError>::Ok(std::move(Response));
}
