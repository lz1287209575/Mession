#pragma once

#include "Common/Net/ServerConnection.h"
#include "Common/Runtime/Object/Object.h"
#include "Common/Runtime/Reflect/Reflection.h"
#include "Servers/World/Domain/PlayerAvatar.h"

MCLASS(Type=Object)
class MPlayerSession : public MObject
{
public:
    MGENERATED_BODY(MPlayerSession, MObject, 0)
public:
    MPlayerSession();

    MPROPERTY(PersistentData | Replicated)
    uint64 PlayerId = 0;

    MPROPERTY(Replicated)
    uint64 GatewayConnectionId = 0;

    MPROPERTY(PersistentData)
    uint32 SessionKey = 0;

    MPROPERTY(PersistentData | Replicated)
    uint32 SceneId = 1;

    MPROPERTY(Replicated)
    uint8 TargetServerType = static_cast<uint8>(EServerType::World);

    void InitializeForLogin(uint64 InPlayerId, uint64 InGatewayConnectionId, uint32 InSessionKey);

    void SetRoute(uint32 InSceneId, uint8 InTargetServerType);

    MPlayerAvatar* GetAvatar() const;

    void VisitReferencedObjects(const TFunction<void(MObject*)>& Visitor) const override;

    MPlayerAvatar* Avatar = nullptr;
};
