#include "Servers/World/Domain/PlayerSession.h"

MPlayerSession::MPlayerSession()
{
    Avatar = CreateDefaultSubObject<MPlayerAvatar>(this, "Avatar");
}

void MPlayerSession::InitializeForLogin(uint64 InPlayerId, uint64 InGatewayConnectionId, uint32 InSessionKey)
{
    PlayerId = InPlayerId;
    GatewayConnectionId = InGatewayConnectionId;
    SessionKey = InSessionKey;
    MarkPropertyDirty("PlayerId");
    MarkPropertyDirty("GatewayConnectionId");
    MarkPropertyDirty("SessionKey");

    if (Avatar)
    {
        Avatar->InitializeForPlayer(PlayerId, SceneId);
    }
}

void MPlayerSession::SetRoute(uint32 InSceneId, uint8 InTargetServerType)
{
    SceneId = InSceneId;
    TargetServerType = InTargetServerType;
    MarkPropertyDirty("SceneId");
    MarkPropertyDirty("TargetServerType");

    if (Avatar)
    {
        Avatar->SetCurrentSceneId(InSceneId);
    }
}

MPlayerAvatar* MPlayerSession::GetAvatar() const
{
    return Avatar;
}

void MPlayerSession::VisitReferencedObjects(const TFunction<void(MObject*)>& Visitor) const
{
    MObject::VisitReferencedObjects(Visitor);
    if (Visitor)
    {
        Visitor(Avatar);
    }
}
