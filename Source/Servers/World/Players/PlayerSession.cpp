#include "Servers/World/Players/PlayerSession.h"

MPlayerSession::MPlayerSession()
{
}

void MPlayerSession::InitializeForLogin(uint64 InPlayerId, uint64 InGatewayConnectionId, uint32 InSessionKey)
{
    PlayerId = InPlayerId;
    GatewayConnectionId = InGatewayConnectionId;
    SessionKey = InSessionKey;
    MarkPropertyDirty("PlayerId");
    MarkPropertyDirty("GatewayConnectionId");
    MarkPropertyDirty("SessionKey");
}

void MPlayerSession::ClearRuntimeState()
{
    GatewayConnectionId = 0;
    SessionKey = 0;
    MarkPropertyDirty("GatewayConnectionId");
    MarkPropertyDirty("SessionKey");
}
