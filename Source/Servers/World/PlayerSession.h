#pragma once

#include "Common/Runtime/Object/Object.h"

class MPlayerAvatar;

MCLASS()
class MPlayerSession : public MObject
{
public:
    MGENERATED_BODY(MPlayerSession, MObject, 0)

public:
    MPlayerSession() { SetClass(StaticClass()); }

    void SetGatewayConnectionId(uint64 InGatewayConnectionId) { GatewayConnectionId = InGatewayConnectionId; }
    uint64 GetGatewayConnectionId() const { return GatewayConnectionId; }

    void SetSessionKey(uint32 InSessionKey) { SessionKey = InSessionKey; }
    uint32 GetSessionKey() const { return SessionKey; }

    void SetCurrentSceneId(uint32 InCurrentSceneId) { CurrentSceneId = InCurrentSceneId; }
    uint32 GetCurrentSceneId() const { return CurrentSceneId; }

    void SetOnline(bool bInOnline) { bOnline = bInOnline; }
    bool IsOnline() const { return bOnline; }

    MPlayerAvatar* GetAvatar() const;

    MPROPERTY(SaveGame)
    uint64 PlayerId = 0;

    MPROPERTY(SaveGame)
    MString Name;

    uint64 GatewayConnectionId = 0;
    uint32 SessionKey = 0;
    uint32 CurrentSceneId = 0;
    bool bOnline = false;
};
