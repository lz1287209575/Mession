#include "Servers/World/Players/Player.h"

MPlayer::MPlayer()
{
    Session = CreateDefaultSubObject<MPlayerSession>(this, "Session");
    Controller = CreateDefaultSubObject<MPlayerController>(this, "Controller");
    Pawn = CreateDefaultSubObject<MPlayerPawn>(this, "Pawn");
    Profile = CreateDefaultSubObject<MPlayerProfile>(this, "Profile");
    CombatProfile = CreateDefaultSubObject<MPlayerCombatProfile>(this, "CombatProfile");
}

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

    if (Controller)
    {
        Controller->InitializeForLogin(ResolveCurrentSceneId());
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

    if (Profile && InSceneId != 0)
    {
        Profile->SetCurrentSceneId(InSceneId);
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

    if (Controller && Controller->SceneId == 0)
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
    if (Pawn && Pawn->IsSpawned())
    {
        return Pawn->Health;
    }

    if (Profile)
    {
        return Profile->ResolveCurrentHealth();
    }

    if (Pawn && Pawn->Health != 0)
    {
        return Pawn->Health;
    }

    return 100;
}

void MPlayer::SyncRuntimeStateToProfile()
{
    if (Profile)
    {
        Profile->SyncRuntimeState(ResolveCurrentSceneId(), ResolveCurrentHealth());
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

MPlayerSession* MPlayer::GetSession() const
{
    return Session;
}

MPlayerController* MPlayer::GetController() const
{
    return Controller;
}

MPlayerPawn* MPlayer::GetPawn() const
{
    return Pawn;
}

MPlayerProfile* MPlayer::GetProfile() const
{
    return Profile;
}

MPlayerCombatProfile* MPlayer::GetCombatProfile() const
{
    return CombatProfile;
}

void MPlayer::VisitReferencedObjects(const TFunction<void(MObject*)>& Visitor) const
{
    MObject::VisitReferencedObjects(Visitor);
    if (Visitor)
    {
        Visitor(Session);
        Visitor(Controller);
        Visitor(Pawn);
        Visitor(Profile);
        Visitor(CombatProfile);
    }
}
