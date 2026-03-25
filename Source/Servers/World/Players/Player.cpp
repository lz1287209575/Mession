#include "Servers/World/Players/Player.h"

MPlayer::MPlayer()
{
    Session = CreateDefaultSubObject<MPlayerSession>(this, "Session");
    Controller = CreateDefaultSubObject<MPlayerController>(this, "Controller");
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

    if (Controller)
    {
        Controller->InitializeForLogin(Profile ? Profile->CurrentSceneId : 1);
    }
}

void MPlayer::SetRoute(uint32 InSceneId, uint8 InTargetServerType)
{
    if (Controller)
    {
        Controller->SetRoute(InSceneId, InTargetServerType);
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

    if (Controller && Profile)
    {
        if (Controller->SceneId == 0)
        {
            Controller->SceneId = Profile->CurrentSceneId != 0 ? Profile->CurrentSceneId : 1;
        }

        if (Profile->CurrentSceneId == 0)
        {
            Profile->CurrentSceneId = Controller->SceneId != 0 ? Controller->SceneId : 1;
        }

        if (Controller->TargetServerType == static_cast<uint8>(EServerType::World))
        {
            Controller->TargetServerType = static_cast<uint8>(EServerType::Scene);
        }
    }
}

MPlayerSession* MPlayer::GetSession() const
{
    return Session;
}

MPlayerController* MPlayer::GetController() const
{
    return Controller;
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
        Visitor(Profile);
    }
}
