#include "WorldServer.h"
#include "Common/Net/ServerRpcRuntime.h"

bool MWorldServer::SendServerMessage(uint64 ConnectionId, uint8 Type, const TByteArray& Payload)
{
    auto It = BackendConnections.find(ConnectionId);
    if (It == BackendConnections.end() || !It->second.Connection)
    {
        return false;
    }

    TByteArray Packet;
    Packet.reserve(1 + Payload.size());
    Packet.push_back(Type);
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

void MWorldServer::BroadcastToScenes(uint8 Type, const TByteArray& Payload)
{
    for (auto& [ConnectionId, Peer] : BackendConnections)
    {
        if (!Peer.bAuthenticated || Peer.ServerType != EServerType::Scene || !Peer.Connection)
        {
            continue;
        }

        SendServerMessage(ConnectionId, Type, Payload);
    }
}

void MWorldServer::HandleLoginServerMessage(uint8 Type, const TByteArray& Data)
{
    // 新的服务器间 RPC：Type 为 MT_RPC，Data 格式：
    // [FunctionId(2)][PayloadSize(4)][Payload...]
    if (Type == static_cast<uint8>(EServerMessageType::MT_RPC))
    {
        if (!TryInvokeServerRpc(&WorldService, Data, ERpcType::ServerToServer))
        {
            LOG_WARN("WorldServer MT_RPC packet could not be handled via reflection");
        }
        return;
    }

    // 兼容旧的基于消息类型的处理（如有）
    LoginMessageDispatcher.Dispatch(Type, Data);
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

    MRpc::TryRpcOrTypedLegacy(
        [&]()
        {
            return MRpc::Call(
                LoginServerConn,
                EServerType::Login,
                "Rpc_OnSessionValidateRequest",
                ValidationRequestId,
                PlayerId,
                SessionKey);
        },
        LoginServerConn,
        EServerMessageType::MT_SessionValidateRequest,
        SSessionValidateRequestMessage{ValidationRequestId, PlayerId, SessionKey});
}

void MWorldServer::HandleRouterServerMessage(uint8 Type, const TByteArray& Data)
{
    if (Type == static_cast<uint8>(EServerMessageType::MT_RPC))
    {
        if (!TryInvokeServerRpc(this, Data, ERpcType::ServerToServer) &&
            !TryInvokeServerRpc(&WorldService, Data, ERpcType::ServerToServer))
        {
            LOG_WARN("WorldServer router MT_RPC packet could not be handled via reflection");
        }
        return;
    }

    RouterMessageDispatcher.Dispatch(Type, Data);
}

void MWorldServer::InitBackendMessageHandlers()
{
    MREGISTER_SERVER_MESSAGE_HANDLER(
        BackendMessageDispatcher,
        EServerMessageType::MT_ServerHandshake,
        &MWorldServer::OnBackend_ServerHandshake,
        "MT_ServerHandshake");

    MREGISTER_SERVER_MESSAGE_HANDLER(
        BackendMessageDispatcher,
        EServerMessageType::MT_Heartbeat,
        &MWorldServer::OnBackend_Heartbeat,
        "MT_Heartbeat");

    MREGISTER_SERVER_MESSAGE_HANDLER(
        BackendMessageDispatcher,
        EServerMessageType::MT_PlayerLogin,
        &MWorldServer::OnBackend_PlayerLogin,
        "MT_PlayerLogin");

    MREGISTER_SERVER_MESSAGE_HANDLER(
        BackendMessageDispatcher,
        EServerMessageType::MT_PlayerLogout,
        &MWorldServer::OnBackend_PlayerLogout,
        "MT_PlayerLogout");

    MREGISTER_SERVER_MESSAGE_HANDLER(
        BackendMessageDispatcher,
        EServerMessageType::MT_PlayerClientSync,
        &MWorldServer::OnBackend_PlayerClientSync,
        "MT_PlayerClientSync");
}

void MWorldServer::InitRouterMessageHandlers()
{
    MREGISTER_SERVER_MESSAGE_HANDLER(
        RouterMessageDispatcher,
        EServerMessageType::MT_ServerRegisterAck,
        &MWorldServer::OnRouter_ServerRegisterAck,
        "MT_ServerRegisterAck");

    MREGISTER_SERVER_MESSAGE_HANDLER(
        RouterMessageDispatcher,
        EServerMessageType::MT_RouteResponse,
        &MWorldServer::OnRouter_RouteResponse,
        "MT_RouteResponse");
}

void MWorldServer::InitLoginMessageHandlers()
{
    MREGISTER_SERVER_MESSAGE_HANDLER(
        LoginMessageDispatcher,
        EServerMessageType::MT_SessionValidateResponse,
        &MWorldServer::OnLogin_SessionValidateResponseMessage,
        "MT_SessionValidateResponse");
}

void MWorldServer::OnBackend_ServerHandshake(uint64 ConnectionId, const SNodeHandshakeMessage& Message)
{
    auto PeerIt = BackendConnections.find(ConnectionId);
    if (PeerIt == BackendConnections.end())
    {
        return;
    }

    SBackendPeer& Peer = PeerIt->second;
    Peer.ServerId = Message.ServerId;
    Peer.ServerType = Message.ServerType;
    Peer.ServerName = Message.ServerName;
    Peer.bAuthenticated = true;

    SendServerMessage(ConnectionId, EServerMessageType::MT_ServerHandshakeAck, SEmptyServerMessage{});
    LOG_INFO("%s authenticated as %d", Peer.ServerName.c_str(), (int)Peer.ServerType);
}

void MWorldServer::OnBackend_Heartbeat(uint64 ConnectionId, const SHeartbeatMessage& /*Message*/)
{
    SendServerMessage(ConnectionId, EServerMessageType::MT_HeartbeatAck, SEmptyServerMessage{});
}

void MWorldServer::OnBackend_PlayerLogin(uint64 ConnectionId, const SPlayerLoginResponseMessage& Message)
{
    auto PeerIt = BackendConnections.find(ConnectionId);
    if (PeerIt == BackendConnections.end())
    {
        return;
    }

    const SBackendPeer& Peer = PeerIt->second;
    if (!Peer.bAuthenticated || Peer.ServerType != EServerType::Gateway)
    {
        return;
    }

    RequestSessionValidation(ConnectionId, Message.PlayerId, Message.SessionKey);
}

void MWorldServer::OnBackend_PlayerLogout(uint64 ConnectionId, const SPlayerLogoutMessage& Message)
{
    auto PeerIt = BackendConnections.find(ConnectionId);
    if (PeerIt == BackendConnections.end())
    {
        return;
    }

    const SBackendPeer& Peer = PeerIt->second;
    if (!Peer.bAuthenticated || Peer.ServerType != EServerType::Gateway)
    {
        return;
    }

    RemovePlayer(Message.PlayerId);
}

void MWorldServer::OnBackend_PlayerClientSync(uint64 ConnectionId, const SPlayerClientSyncMessage& Message)
{
    auto PeerIt = BackendConnections.find(ConnectionId);
    if (PeerIt == BackendConnections.end())
    {
        return;
    }

    const SBackendPeer& Peer = PeerIt->second;
    if (!Peer.bAuthenticated || Peer.ServerType != EServerType::Gateway)
    {
        return;
    }

    HandleGameplayPacket(Message.PlayerId, Message.Data);
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

void MWorldServer::OnLogin_SessionValidateResponseMessage(const SSessionValidateResponseMessage& Message)
{
    OnLogin_SessionValidateResponse(Message.ConnectionId, Message.PlayerId, Message.bValid);
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

    FinalizePlayerLogin(
        PlayerId,
        Pending.GatewayConnectionId,
        Pending.SessionKey,
        false,
        0,
        "",
        "");

    if (Config.EnableMgoPersistence && MgoServerConn && MgoServerConn->IsConnected())
    {
        (void)RequestMgoLoad(PlayerId, Pending.GatewayConnectionId, Pending.SessionKey);
    }
}
