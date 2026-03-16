#include "RouterServer.h"
#include "Common/Config.h"
#include "Common/ServerRpcRuntime.h"
#include "Core/Net/Socket.h"
#include "Core/Net/HttpDebugServer.h"
#include "Core/Json.h"

namespace
{
const TMap<FString, const char*> RouterEnvMap = {
    {"port", "MESSION_ROUTER_PORT"},
    {"route_lease_seconds", "MESSION_ROUTE_LEASE_SECONDS"},
    {"debug_http_port", "MESSION_ROUTER_DEBUG_HTTP_PORT"},
};

const SRpcEndpointBinding GRouterServerRegisterAckEndpoints[] = {
    {EServerType::Gateway, "MGatewayServer", "Rpc_OnRouterServerRegisterAck"},
    {EServerType::Login, "MLoginServer", "Rpc_OnRouterServerRegisterAck"},
    {EServerType::World, "MWorldServer", "Rpc_OnRouterServerRegisterAck"},
};

const SRpcEndpointBinding GRouterRouteResponseEndpoints[] = {
    {EServerType::Gateway, "MGatewayServer", "Rpc_OnRouterRouteResponse"},
    {EServerType::World, "MWorldServer", "Rpc_OnRouterRouteResponse"},
};
}

bool MRouterServer::LoadConfig(const FString& ConfigPath)
{
    TMap<FString, FString> Vars;
    if (!ConfigPath.empty())
    {
        MConfig::LoadFromFile(ConfigPath, Vars);
    }
    MConfig::ApplyEnvOverrides(Vars, RouterEnvMap);
    Config.ListenPort = MConfig::GetU16(Vars, "port", Config.ListenPort);
    Config.RouteLeaseSeconds = MConfig::GetU16(Vars, "route_lease_seconds", Config.RouteLeaseSeconds);
    Config.DebugHttpPort = MConfig::GetU16(Vars, "debug_http_port", Config.DebugHttpPort);
    return true;
}

bool MRouterServer::Init(int InPort)
{
    if (InPort > 0)
    {
        Config.ListenPort = static_cast<uint16>(InPort);
    }
    bRunning = true;

    MLogger::LogStartupBanner("RouterServer", Config.ListenPort, 0);
    InitPeerMessageHandlers();
    
    // 启动调试 HTTP 服务器（仅当配置端口 > 0 时）
    if (Config.DebugHttpPort > 0)
    {
        DebugServer = TUniquePtr<MHttpDebugServer>(new MHttpDebugServer(
            Config.DebugHttpPort,
            [this]() { return BuildDebugStatusJson(); }));
        DebugServer->Start();
    }

    return true;
}

uint16 MRouterServer::GetListenPort() const
{
    return Config.ListenPort;
}

void MRouterServer::OnAccept(uint64 ConnId, TSharedPtr<INetConnection> Conn)
{
    Conn->SetNonBlocking(true);
    SRouterPeer Peer;
    Peer.Connection = Conn;
    MTcpConnection* Tcp = dynamic_cast<MTcpConnection*>(Conn.get());
    Peer.Address = Tcp ? Tcp->GetRemoteAddress() : "?";
    Peers[ConnId] = Peer;
    LOG_INFO("New router peer connected: %s (connection_id=%llu)", Peer.Address.c_str(), (unsigned long long)ConnId);
    EventLoop.RegisterConnection(ConnId, Conn,
        [this](uint64 Id, const TArray& Payload)
        {
            HandlePacket(Id, Payload);
        },
        [this](uint64 Id)
        {
            RemovePeer(Id);
        });
}

void MRouterServer::TickBackends()
{
    ++TickCounter;
}

void MRouterServer::ShutdownConnections()
{
    for (auto& [ConnectionId, Peer] : Peers)
    {
        if (Peer.Connection)
        {
            Peer.Connection->Close();
        }
    }
    Peers.clear();
    if (DebugServer)
    {
        DebugServer->Stop();
        DebugServer.reset();
    }
    LOG_INFO("Router server shutdown complete");
}

void MRouterServer::OnRunStarted()
{
    LOG_INFO("Router server running...");
}

void MRouterServer::Tick()
{
    if (!bRunning)
    {
        return;
    }
    // 由 EventLoop.RunOnce 驱动，此处仅保留接口兼容
}

FString MRouterServer::BuildDebugStatusJson() const
{
    size_t RegisteredCount = 0;
    for (const auto& [Id, Peer] : Peers)
    {
        (void)Id;
        if (Peer.bRegistered)
        {
            ++RegisteredCount;
        }
    }

    MJsonWriter W = MJsonWriter::Object();
    W.Key("server"); W.Value("Router");
    W.Key("peers"); W.Value(static_cast<uint64>(Peers.size()));
    W.Key("registeredPeers"); W.Value(static_cast<uint64>(RegisteredCount));
    return W.ToString();
}

void MRouterServer::HandlePacket(uint64 ConnectionId, const TArray& Data)
{
    if (Data.empty())
    {
        return;
    }

    auto PeerIt = Peers.find(ConnectionId);
    if (PeerIt == Peers.end())
    {
        return;
    }

    const uint8 MsgType = Data[0];
    const TArray Payload(Data.begin() + 1, Data.end());
    PeerMessageDispatcher.Dispatch(ConnectionId, MsgType, Payload);
}

void MRouterServer::InitPeerMessageHandlers()
{
    MREGISTER_SERVER_MESSAGE_HANDLER(
        PeerMessageDispatcher,
        EServerMessageType::MT_ServerHandshake,
        &MRouterServer::OnPeer_ServerHandshake,
        "MT_ServerHandshake");

    MREGISTER_SERVER_MESSAGE_HANDLER(
        PeerMessageDispatcher,
        EServerMessageType::MT_Heartbeat,
        &MRouterServer::OnPeer_Heartbeat,
        "MT_Heartbeat");

    MREGISTER_SERVER_MESSAGE_HANDLER(
        PeerMessageDispatcher,
        EServerMessageType::MT_ServerRegister,
        &MRouterServer::OnPeer_ServerRegister,
        "MT_ServerRegister");

    MREGISTER_SERVER_MESSAGE_HANDLER(
        PeerMessageDispatcher,
        EServerMessageType::MT_ServerLoadReport,
        &MRouterServer::OnPeer_ServerLoadReport,
        "MT_ServerLoadReport");

    MREGISTER_SERVER_MESSAGE_HANDLER(
        PeerMessageDispatcher,
        EServerMessageType::MT_RouteQuery,
        &MRouterServer::OnPeer_RouteQuery,
        "MT_RouteQuery");
}

void MRouterServer::OnPeer_ServerHandshake(uint64 ConnectionId, const SServerHandshakeMessage& Message)
{
    auto PeerIt = Peers.find(ConnectionId);
    if (PeerIt == Peers.end())
    {
        return;
    }

    SRouterPeer& Peer = PeerIt->second;
    Peer.ServerId = Message.ServerId;
    Peer.ServerType = Message.ServerType;
    Peer.ServerName = Message.ServerName;
    Peer.bAuthenticated = true;

    SendServerMessage(ConnectionId, EServerMessageType::MT_ServerHandshakeAck, SEmptyServerMessage{});
    LOG_INFO("Router authenticated %s (id=%u type=%d)",
             Peer.ServerName.c_str(), Peer.ServerId, (int)Peer.ServerType);
}

void MRouterServer::OnPeer_Heartbeat(uint64 ConnectionId, const SHeartbeatMessage& /*Message*/)
{
    SendServerMessage(ConnectionId, EServerMessageType::MT_HeartbeatAck, SEmptyServerMessage{});
}

void MRouterServer::OnPeer_ServerRegister(uint64 ConnectionId, const SServerRegisterMessage& Message)
{
    auto PeerIt = Peers.find(ConnectionId);
    if (PeerIt == Peers.end() || !PeerIt->second.bAuthenticated)
    {
        return;
    }

    SRouterPeer& Peer = PeerIt->second;
    Peer.ServerId = Message.ServerId;
    Peer.ServerType = Message.ServerType;
    Peer.ServerName = Message.ServerName;
    Peer.Address = Message.Address;
    Peer.Port = Message.Port;
    Peer.ZoneId = Message.ZoneId;
    Peer.bRegistered = true;

    bool bSentAckByRpc = false;
    if (const SRpcEndpointBinding* Endpoint = FindRpcEndpointByServerType(
            GRouterServerRegisterAckEndpoints,
            sizeof(GRouterServerRegisterAckEndpoints) / sizeof(GRouterServerRegisterAckEndpoints[0]),
            Peer.ServerType))
    {
        TArray RpcData;
        if (BuildRpcPayloadForEndpoint(*Endpoint, BuildRpcArgsPayload(static_cast<uint8>(1)), RpcData))
        {
            bSentAckByRpc = SendServerMessage(ConnectionId, static_cast<uint8>(EServerMessageType::MT_RPC), RpcData);
        }
    }

    if (!bSentAckByRpc)
    {
        SendServerMessage(ConnectionId, EServerMessageType::MT_ServerRegisterAck, SServerRegisterAckMessage{1});
    }

    LOG_INFO("Registered server %s (id=%u type=%d addr=%s:%u)",
             Peer.ServerName.c_str(),
             Peer.ServerId,
             (int)Peer.ServerType,
             Peer.Address.c_str(),
             Peer.Port);
}

void MRouterServer::OnPeer_ServerLoadReport(uint64 ConnectionId, const SServerLoadReportMessage& Message)
{
    auto PeerIt = Peers.find(ConnectionId);
    if (PeerIt == Peers.end() || !PeerIt->second.bAuthenticated)
    {
        return;
    }

    SRouterPeer& Peer = PeerIt->second;
    Peer.CurrentLoad = Message.CurrentLoad;
    Peer.Capacity = (Message.Capacity > 0) ? Message.Capacity : 1;
}

void MRouterServer::OnPeer_RouteQuery(uint64 ConnectionId, const SRouteQueryMessage& Query)
{
    auto PeerIt = Peers.find(ConnectionId);
    if (PeerIt == Peers.end() || !PeerIt->second.bAuthenticated)
    {
        return;
    }

    const SRouterPeer& Peer = PeerIt->second;
    const SRouterPeer* Target = SelectRouteTarget(Query.RequestedType, Query.PlayerId, Query.ZoneId);

    SRouteResponseMessage Response;
    Response.RequestId = Query.RequestId;
    Response.RequestedType = Query.RequestedType;
    Response.PlayerId = Query.PlayerId;
    Response.bFound = (Target != nullptr);
    if (Target)
    {
        Response.ServerInfo = SServerInfo(Target->ServerId, Target->ServerType, Target->ServerName, Target->Address, Target->Port, Target->ZoneId);
    }

    bool bSentResponseByRpc = false;
    if (const SRpcEndpointBinding* Endpoint = FindRpcEndpointByServerType(
            GRouterRouteResponseEndpoints,
            sizeof(GRouterRouteResponseEndpoints) / sizeof(GRouterRouteResponseEndpoints[0]),
            Peer.ServerType))
    {
        TArray RpcData;
        if (BuildRpcPayloadForEndpoint(
                *Endpoint,
                BuildRpcArgsPayload(
                    Response.RequestId,
                    static_cast<uint8>(Response.RequestedType),
                    Response.PlayerId,
                    Response.bFound,
                    Response.ServerInfo.ServerId,
                    static_cast<uint8>(Response.ServerInfo.ServerType),
                    Response.ServerInfo.ServerName,
                    Response.ServerInfo.Address,
                    Response.ServerInfo.Port,
                    Response.ServerInfo.ZoneId),
                RpcData))
        {
            bSentResponseByRpc = SendServerMessage(ConnectionId, static_cast<uint8>(EServerMessageType::MT_RPC), RpcData);
        }
    }

    if (!bSentResponseByRpc)
    {
        SendServerMessage(ConnectionId, EServerMessageType::MT_RouteResponse, Response);
    }

    LOG_INFO("Route query from %s for type=%d player=%llu result=%s",
             Peer.ServerName.c_str(),
             (int)Query.RequestedType,
             (unsigned long long)Query.PlayerId,
             Target ? Target->ServerName.c_str() : "none");
}

bool MRouterServer::SendServerMessage(uint64 ConnectionId, uint8 Type, const TArray& Payload)
{
    auto It = Peers.find(ConnectionId);
    if (It == Peers.end() || !It->second.Connection)
    {
        return false;
    }

    TArray Packet;
    Packet.reserve(1 + Payload.size());
    Packet.push_back(Type);
    Packet.insert(Packet.end(), Payload.begin(), Payload.end());
    return It->second.Connection->Send(Packet.data(), Packet.size());
}

const SRouterPeer* MRouterServer::SelectRouteTarget(EServerType RequestedType, uint64 PlayerId, uint16 ZoneId)
{
    if (RequestedType == EServerType::World && PlayerId != 0)
    {
        auto BindingIt = PlayerRouteBindings.find(PlayerId);
        if (BindingIt != PlayerRouteBindings.end())
        {
            const bool bLeaseExpired = (Config.RouteLeaseSeconds > 0) &&
                (TickCounter >= BindingIt->second.LeaseExpireTick);
            if (!bLeaseExpired)
            {
                const SRouterPeer* BoundServer = FindRegisteredServerById(BindingIt->second.WorldServerId);
                if (BoundServer)
                {
                    return BoundServer;
                }
            }
            PlayerRouteBindings.erase(BindingIt);
        }
    }

    const SRouterPeer* Selected = nullptr;
    for (const auto& [ConnectionId, Peer] : Peers)
    {
        (void)ConnectionId;
        if (!Peer.bRegistered || !Peer.Connection || !Peer.Connection->IsConnected())
        {
            continue;
        }
        if (Peer.ServerType != RequestedType)
        {
            continue;
        }

        if ((RequestedType == EServerType::World || RequestedType == EServerType::Scene) &&
            ZoneId != 0 && Peer.ZoneId != ZoneId)
        {
            continue;
        }

        if (RequestedType == EServerType::World)
        {
            const uint32 PeerCapacity = (Peer.Capacity > 0) ? Peer.Capacity : 1;
            const uint32 SelectedCapacity = Selected ? ((Selected->Capacity > 0) ? Selected->Capacity : 1) : 0;
            const float PeerLoadRatio = static_cast<float>(Peer.CurrentLoad) / static_cast<float>(PeerCapacity);
            const float SelectedLoadRatio = Selected ? (static_cast<float>(Selected->CurrentLoad) / static_cast<float>(SelectedCapacity)) : 1.0f;
            if (!Selected || PeerLoadRatio < SelectedLoadRatio)
            {
                Selected = &Peer;
            }
            else if (Selected && PeerLoadRatio == SelectedLoadRatio && Peer.ServerId < Selected->ServerId)
            {
                Selected = &Peer;
            }
        }
        else
        {
            if (!Selected || Peer.ServerId < Selected->ServerId)
            {
                Selected = &Peer;
            }
        }
    }

    if (Selected && RequestedType == EServerType::World && PlayerId != 0)
    {
        SPlayerRouteBinding Binding;
        Binding.PlayerId = PlayerId;
        Binding.WorldServerId = Selected->ServerId;
        Binding.LeaseExpireTick = TickCounter + static_cast<uint64>(Config.RouteLeaseSeconds) * 60;
        PlayerRouteBindings[PlayerId] = Binding;
    }

    return Selected;
}

const SRouterPeer* MRouterServer::FindRegisteredServerById(uint32 ServerId) const
{
    for (const auto& [ConnectionId, Peer] : Peers)
    {
        (void)ConnectionId;
        if (!Peer.bRegistered || !Peer.Connection || !Peer.Connection->IsConnected())
        {
            continue;
        }
        if (Peer.ServerId == ServerId)
        {
            return &Peer;
        }
    }

    return nullptr;
}

void MRouterServer::RemovePeer(uint64 ConnectionId)
{
    auto It = Peers.find(ConnectionId);
    if (It == Peers.end())
    {
        return;
    }

    const uint32 RemovedServerId = It->second.ServerId;
    const EServerType RemovedServerType = It->second.ServerType;

    if (It->second.bRegistered)
    {
        LOG_INFO("Router peer removed: %s (id=%u type=%d)",
                 It->second.ServerName.c_str(),
                 It->second.ServerId,
                 (int)It->second.ServerType);
    }
    else
    {
        LOG_INFO("Router connection removed: %llu", (unsigned long long)ConnectionId);
    }

    Peers.erase(It);

    if (RemovedServerType == EServerType::World)
    {
        TVector<uint64> PlayersToUnbind;
        for (const auto& [PlayerId, Binding] : PlayerRouteBindings)
        {
            if (Binding.WorldServerId == RemovedServerId)
            {
                PlayersToUnbind.push_back(PlayerId);
            }
        }

        for (uint64 PlayerId : PlayersToUnbind)
        {
            PlayerRouteBindings.erase(PlayerId);
        }
    }
}
