#include "Servers/World/Player/Player.h"

#include "Protocol/Messages/Combat/CombatSceneMessages.h"
#include "Protocol/Messages/Scene/SceneSyncMessages.h"
#include "Servers/World/Player/PlayerCombatProfile.h"
#include "Servers/World/Player/PlayerController.h"
#include "Servers/World/Player/PlayerPawn.h"
#include "Servers/World/Player/PlayerProfile.h"
#include "Servers/World/Player/PlayerProgression.h"
#include "Servers/World/Player/PlayerSession.h"

void MPlayer::InitializeForLogin(uint64 InPlayerId, uint64 InGatewayConnectionId, uint32 InSessionKey)
{
    PlayerId = InPlayerId;
    MarkPropertyDirty("PlayerId");

    if (Session)
    {
        Session->InitializeForLogin(InPlayerId, InGatewayConnectionId, InSessionKey);
    }

    if (Profile)
    {
        Profile->InitializeForPlayer(PlayerId, Profile->CurrentSceneId);
    }

    if (Pawn)
    {
        Pawn->InitializeForLogin(0, ResolveCurrentHealth());
    }
}

void MPlayer::SetRoute(uint32 InSceneId, uint8 InTargetServerType)
{
    if (Controller)
    {
        Controller->SetRoute(InSceneId, InTargetServerType);
    }

    if (Pawn)
    {
        if (InTargetServerType == static_cast<uint8>(EServerType::Scene) && InSceneId != 0)
        {
            Pawn->Spawn(InSceneId, ResolveCurrentHealth());
        }
        else
        {
            Pawn->Despawn();
        }
    }
}

void MPlayer::FinalizeLoadedState()
{
    if (Profile)
    {
        if (Profile->PlayerId != 0)
        {
            PlayerId = Profile->PlayerId;
        }
        else if (PlayerId != 0)
        {
            Profile->InitializeForPlayer(PlayerId, Profile->CurrentSceneId);
        }
    }

    const uint32 ResolvedSceneId = ResolveCurrentSceneId();
    const uint32 ResolvedHealth = ResolveCurrentHealth();

    if (Controller)
    {
        Controller->InitializeForLogin(ResolvedSceneId);
    }

    if (Profile && Profile->CurrentSceneId == 0)
    {
        Profile->SetCurrentSceneId(ResolvedSceneId);
    }

    if (Pawn)
    {
        Pawn->SyncFromPersistence(0, ResolvedHealth);
    }
}

uint32 MPlayer::ResolveCurrentSceneId() const
{
    if (Pawn && Pawn->IsSpawned() && Pawn->SceneId != 0)
    {
        return Pawn->SceneId;
    }

    if (Controller && Controller->SceneId != 0)
    {
        return Controller->SceneId;
    }

    if (Profile)
    {
        return Profile->ResolveCurrentSceneId();
    }

    return 1;
}

uint32 MPlayer::ResolveCurrentHealth() const
{
    if (Profile)
    {
        return Profile->ResolveCurrentHealth();
    }

    if (Pawn && Pawn->IsSpawned())
    {
        return Pawn->Health;
    }

    return 100;
}

uint64 MPlayer::ResolveGatewayConnectionId() const
{
    return Session ? Session->GatewayConnectionId : 0;
}

bool MPlayer::CanReceiveSceneNotify() const
{
    return ResolveGatewayConnectionId() != 0;
}

bool MPlayer::TryBuildSceneState(SPlayerSceneStateMessage& OutMessage) const
{
    if (!Pawn || !Pawn->IsSpawned() || Pawn->SceneId == 0)
    {
        return false;
    }

    OutMessage.PlayerId = PlayerId;
    OutMessage.SceneId = static_cast<uint16>(Pawn->SceneId);
    OutMessage.X = Pawn->X;
    OutMessage.Y = Pawn->Y;
    OutMessage.Z = Pawn->Z;
    return true;
}

bool MPlayer::TryBuildCombatAvatarSnapshot(uint32 SceneId, FSceneCombatAvatarSnapshot& OutSnapshot) const
{
    if (!CombatProfile || SceneId == 0)
    {
        return false;
    }

    OutSnapshot = CombatProfile->BuildSceneAvatarSnapshot(PlayerId, SceneId, ResolveCurrentHealth());
    return true;
}

uint32 MPlayer::ApplyCombatResult(const FWorldCommitCombatResultRequest& Request)
{
    uint32 ResolvedHealth = Request.CommittedHealth;
    if (CombatProfile)
    {
        ResolvedHealth = CombatProfile->RecordCommittedCombatResult(Request);
    }

    ApplyResolvedHealth(ResolvedHealth);
    return ResolvedHealth;
}

void MPlayer::ApplyResolvedHealth(uint32 InHealth)
{
    if (Profile)
    {
        if (MPlayerProgression* Progression = Profile->GetProgression())
        {
            Progression->SetHealth(InHealth);
        }
    }

    if (Pawn && Pawn->IsSpawned())
    {
        Pawn->SetHealth(InHealth);
    }
}

void MPlayer::SyncRuntimeStateToProfile()
{
    if (Profile)
    {
        Profile->SyncRuntimeState(ResolveCurrentSceneId());
    }
}

void MPlayer::PrepareForLogout()
{
    SyncRuntimeStateToProfile();

    if (Pawn)
    {
        Pawn->Despawn();
    }

    if (Session)
    {
        Session->ClearRuntimeState();
    }
}

MFuture<TResult<FPlayerFindResponse, FAppError>> MPlayer::PlayerFind(const FPlayerFindRequest& /*Request*/)
{
    FPlayerFindResponse Response;
    Response.bFound = true;
    Response.PlayerId = PlayerId;

    if (Session)
    {
        Response.GatewayConnectionId = Session->GatewayConnectionId;
    }

    Response.SceneId = ResolveCurrentSceneId();
    return MServerCallAsyncSupport::MakeSuccessFuture(std::move(Response));
}
