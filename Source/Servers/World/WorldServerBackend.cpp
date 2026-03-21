#include "WorldServer.h"
#include "Common/Net/ServerRpcRuntime.h"
#include "Common/Runtime/HexUtils.h"

bool MWorldServer::SendServerPacket(uint64 ConnectionId, uint8 PacketType, const TByteArray& Payload)
{
    auto It = BackendConnections.find(ConnectionId);
    if (It == BackendConnections.end() || !It->second.Connection)
    {
        return false;
    }

    TByteArray Packet;
    Packet.reserve(1 + Payload.size());
    Packet.push_back(PacketType);
    Packet.insert(Packet.end(), Payload.begin(), Payload.end());
    return It->second.Connection->Send(Packet.data(), Packet.size());
}

uint64 MWorldServer::FindAuthenticatedBackendConnectionId(EServerType ServerType) const
{
    for (const auto& [ConnectionId, Peer] : BackendConnections)
    {
        if (Peer.bAuthenticated && Peer.ServerType == ServerType && Peer.Connection)
        {
            return ConnectionId;
        }
    }

    return 0;
}

void MWorldServer::HandleLoginServerPacket(uint8 PacketType, const TByteArray& Data)
{
    if (PacketType == static_cast<uint8>(EServerMessageType::MT_RPC))
    {
        if (!TryInvokeServerRpc(this, Data, ERpcType::ServerToServer))
        {
            LOG_WARN("WorldServer MT_RPC packet could not be handled via reflection");
        }
        return;
    }

    LOG_WARN("Unexpected non-RPC login server message type %u", static_cast<unsigned>(PacketType));
}

void MWorldServer::RequestSessionValidation(uint64 GatewayConnectionId, uint64 PlayerId, uint32 SessionKey)
{
    if (!LoginServerConn || !LoginServerConn->IsConnected())
    {
        LOG_WARN("Login server unavailable, cannot validate session for player %llu",
                 (unsigned long long)PlayerId);
        return;
    }

    uint64 ValidationRequestId = NextSessionValidationId++;
    if (ValidationRequestId == 0)
    {
        ValidationRequestId = NextSessionValidationId++;
    }

    PendingSessionValidations[ValidationRequestId] = {ValidationRequestId, GatewayConnectionId, PlayerId, SessionKey};

    const bool bSent = MRpc::Call(
        LoginServerConn,
        EServerType::Login,
        "Rpc_OnSessionValidateRequest",
        ValidationRequestId,
        PlayerId,
        SessionKey);
    if (!bSent)
    {
        PendingSessionValidations.erase(ValidationRequestId);
        LOG_WARN("World->Login session validate RPC send failed: request=%llu player=%llu",
                 static_cast<unsigned long long>(ValidationRequestId),
                 static_cast<unsigned long long>(PlayerId));
    }
}

void MWorldServer::HandleRouterServerPacket(uint8 PacketType, const TByteArray& Data)
{
    if (PacketType == static_cast<uint8>(EServerMessageType::MT_RPC))
    {
        if (!TryInvokeServerRpc(this, Data, ERpcType::ServerToServer))
        {
            LOG_WARN("WorldServer router MT_RPC packet could not be handled via reflection");
        }
        return;
    }

    LOG_WARN("Unexpected non-RPC router message type %u", static_cast<unsigned>(PacketType));
}

void MWorldServer::Rpc_OnServerHandshake(uint32 ServerId, uint8 ServerTypeValue, const MString& ServerName)
{
    const uint64 ConnectionId = GetCurrentServerRpcConnectionId();
    auto PeerIt = BackendConnections.find(ConnectionId);
    if (PeerIt == BackendConnections.end())
    {
        return;
    }

    SBackendPeer& Peer = PeerIt->second;
    Peer.ServerId = ServerId;
    Peer.ServerType = static_cast<EServerType>(ServerTypeValue);
    Peer.ServerName = ServerName;
    Peer.bAuthenticated = true;

    LOG_INFO("%s authenticated as %d", Peer.ServerName.c_str(), (int)Peer.ServerType);
}

void MWorldServer::Rpc_OnHeartbeat(uint32 Sequence)
{
    LOG_DEBUG("WorldServer heartbeat received (connection=%llu seq=%u)",
              static_cast<unsigned long long>(GetCurrentServerRpcConnectionId()),
              static_cast<unsigned>(Sequence));
}

void MWorldServer::Rpc_OnPlayerLogout(uint64 PlayerId)
{
    RemovePlayer(PlayerId);
}

void MWorldServer::Rpc_OnPlayerClientSync(uint64 PlayerId, const MString& PacketHex)
{
    TByteArray Data;
    if (!Hex::TryDecodeHex(PacketHex, Data))
    {
        LOG_WARN("Gateway client sync hex decode failed for player %llu", (unsigned long long)PlayerId);
        return;
    }

    HandleGameplayPacket(PlayerId, Data);
}

void MWorldServer::OnRouter_ServerRegisterAck(const SNodeRegisterAckMessage& /*Message*/)
{
    LOG_INFO("World server registered to RouterServer");
}

void MWorldServer::Rpc_OnRouterServerRegisterAck(uint8 Result)
{
    OnRouter_ServerRegisterAck(SNodeRegisterAckMessage{Result});
}

void MWorldServer::OnRouter_RouteResponse(const SRouteResponseMessage& Message)
{
    if (Message.PlayerId != 0 || !Message.bFound)
    {
        return;
    }

    if (Message.RequestedType == EServerType::Login)
    {
        ApplyLoginServerRoute(
            Message.ServerInfo.ServerId,
            Message.ServerInfo.ServerName,
            Message.ServerInfo.Address,
            Message.ServerInfo.Port);
        return;
    }

    if (Message.RequestedType == EServerType::Mgo)
    {
        ApplyMgoServerRoute(
            Message.ServerInfo.ServerId,
            Message.ServerInfo.ServerName,
            Message.ServerInfo.Address,
            Message.ServerInfo.Port);
    }
}

void MWorldServer::Rpc_OnRouterRouteResponse(
    uint64 RequestId,
    uint8 RequestedTypeValue,
    uint64 PlayerId,
    bool bFound,
    uint32 ServerId,
    uint8 ServerTypeValue,
    const MString& ServerName,
    const MString& Address,
    uint16 Port,
    uint16 ZoneId)
{
    SRouteResponseMessage Message;
    Message.RequestId = RequestId;
    Message.RequestedType = static_cast<EServerType>(RequestedTypeValue);
    Message.PlayerId = PlayerId;
    Message.bFound = bFound;
    if (bFound)
    {
        Message.ServerInfo = SServerInfo(
            ServerId,
            static_cast<EServerType>(ServerTypeValue),
            ServerName,
            Address,
            Port,
            ZoneId);
    }

    OnRouter_RouteResponse(Message);
}

void MWorldServer::OnLogin_SessionValidateResponse(uint64 ConnectionId, uint64 PlayerId, bool bValid)
{
    auto PendingIt = PendingSessionValidations.find(ConnectionId);
    if (PendingIt == PendingSessionValidations.end())
    {
        return;
    }

    const SPendingSessionValidation Pending = PendingIt->second;
    PendingSessionValidations.erase(PendingIt);

    if (!bValid || Pending.PlayerId != PlayerId)
    {
        LOG_WARN("Session validation failed for player %llu on connection %llu",
                 (unsigned long long)Pending.PlayerId,
                 (unsigned long long)ConnectionId);
        return;
    }

    BeginLoadPlayerState(PlayerId, Pending.GatewayConnectionId, Pending.SessionKey);
}
