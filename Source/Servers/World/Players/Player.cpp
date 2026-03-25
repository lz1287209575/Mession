#include "Servers/World/Players/Player.h"

MPlayer::MPlayer()
{
    Session = CreateDefaultSubObject<MPlayerSession>(this, "Session");
    Controller = CreateDefaultSubObject<MPlayerController>(this, "Controller");
    Pawn = CreateDefaultSubObject<MPlayerPawn>(this, "Pawn");
    Profile = CreateDefaultSubObject<MPlayerProfile>(this, "Profile");
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

    uint32 InitialSceneId = 1;
    uint32 InitialHealth = 100;
    if (Profile)
    {
        InitialSceneId = Profile->CurrentSceneId != 0 ? Profile->CurrentSceneId : 1;
        if (MPlayerProgression* Progression = Profile->GetProgression())
        {
            InitialHealth = Progression->Health;
        }
    }

    if (Controller)
    {
        Controller->InitializeForLogin(InitialSceneId);
    }

    if (Pawn)
    {
        Pawn->InitializeForLogin(InitialSceneId, InitialHealth);
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
        Pawn->SetSceneId(InSceneId);
    }

    if (Profile)
    {
        Profile->SetCurrentSceneId(InSceneId);
    }
}

void MPlayer::FinalizeLoadedState()
{
    if (Profile)
    {
        PlayerId = Profile->PlayerId != 0 ? Profile->PlayerId : PlayerId;
    }

    uint32 ResolvedSceneId = 1;
    if (Controller && Controller->SceneId != 0)
    {
        ResolvedSceneId = Controller->SceneId;
    }
    else if (Profile && Profile->CurrentSceneId != 0)
    {
        ResolvedSceneId = Profile->CurrentSceneId;
    }

    uint32 ResolvedHealth = 100;
    if (Profile)
    {
        if (MPlayerProgression* Progression = Profile->GetProgression())
        {
            ResolvedHealth = Progression->Health;
        }
    }

    if (Controller && Profile)
    {
        if (Controller->SceneId == 0)
        {
            Controller->SceneId = ResolvedSceneId;
        }

        if (Profile->CurrentSceneId == 0)
        {
            Profile->CurrentSceneId = ResolvedSceneId;
        }

        if (Controller->TargetServerType == static_cast<uint8>(EServerType::World))
        {
            Controller->TargetServerType = static_cast<uint8>(EServerType::Scene);
        }
    }

    if (Pawn)
    {
        Pawn->SyncFromPersistence(ResolvedSceneId, ResolvedHealth);
    }
}

MFuture<TResult<FPlayerApplyRouteResponse, FAppError>> MPlayer::ApplyRouteCall(const FPlayerApplyRouteRequest& Request)
{
    if (Request.SceneId == 0)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerApplyRouteResponse>("scene_id_required", "ApplyRouteCall");
    }

    SetRoute(Request.SceneId, Request.TargetServerType);

    FPlayerApplyRouteResponse Response;
    Response.PlayerId = PlayerId;
    Response.SceneId = Request.SceneId;
    Response.TargetServerType = Request.TargetServerType;
    return MServerCallAsyncSupport::MakeSuccessFuture(std::move(Response));
}

MFuture<TResult<FPlayerQueryStateResponse, FAppError>> MPlayer::QueryStateCall(const FPlayerQueryStateRequest& /*Request*/)
{
    FPlayerQueryStateResponse Response;
    Response.PlayerId = PlayerId;

    if (Session)
    {
        Response.GatewayConnectionId = Session->GatewayConnectionId;
    }

    if (Controller && Controller->SceneId != 0)
    {
        Response.SceneId = Controller->SceneId;
    }
    else if (Pawn && Pawn->SceneId != 0)
    {
        Response.SceneId = Pawn->SceneId;
    }
    else if (Profile)
    {
        Response.SceneId = Profile->CurrentSceneId;
    }

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

void MPlayer::VisitReferencedObjects(const TFunction<void(MObject*)>& Visitor) const
{
    MObject::VisitReferencedObjects(Visitor);
    if (Visitor)
    {
        Visitor(Session);
        Visitor(Controller);
        Visitor(Pawn);
        Visitor(Profile);
    }
}
