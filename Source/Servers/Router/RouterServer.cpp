#include "RouterServer.h"
#include "Common/Runtime/Config.h"
#include "Common/Net/ServerRpcRuntime.h"
#include "Common/IO/Socket/Socket.h"
#include "Common/Net/HttpDebugServer.h"
#include "Common/Runtime/Json.h"

#include <algorithm>

namespace
{
const TMap<MString, const char*> RouterEnvMap = {
    {"port", "MESSION_ROUTER_PORT"},
    {"route_lease_seconds", "MESSION_ROUTE_LEASE_SECONDS"},
    {"debug_http_port", "MESSION_ROUTER_DEBUG_HTTP_PORT"},
};

}

bool MRouterServer::LoadConfig(const MString& ConfigPath)
{
    TMap<MString, MString> Vars;
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
    
    // 启动调试 HTTP 服务器（仅当配置端口 > 0 时）
    if (Config.DebugHttpPort > 0)
    {
        DebugServer = TUniquePtr<MHttpDebugServer>(new MHttpDebugServer(
            Config.DebugHttpPort,
            [this]() { return BuildDebugStatusJson(); }));
        if (!DebugServer->Start())
        {
            LOG_ERROR("Router debug HTTP failed to start on port %u", static_cast<unsigned>(Config.DebugHttpPort));
            DebugServer.reset();
        }
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
        [this](uint64 Id, const TByteArray& Payload)
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

MString MRouterServer::BuildDebugStatusJson() const
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
    W.Key("rpcManifestEntries"); W.Value(static_cast<uint64>(GetGeneratedRpcEntryCount()));
    W.Key("rpcSupport");
    W.BeginObject();
    for (EServerType ServerType : {EServerType::Gateway, EServerType::Login, EServerType::World})
    {
        W.Key(GetServerTypeDisplayName(ServerType));
        W.BeginArray();
        for (const MString& Name : GetGeneratedRpcFunctionNames(ServerType))
        {
            W.Value(Name);
        }
        W.EndArray();
    }
    W.EndObject();
    W.Key("unsupportedRpc");
    W.BeginArray();
    for (const SGeneratedRpcUnsupportedStat& Stat : GetGeneratedRpcUnsupportedStats())
    {
        W.BeginObject();
        W.Key("serverType"); W.Value(GetServerTypeDisplayName(Stat.ServerType));
        W.Key("function"); W.Value(Stat.FunctionName);
        W.Key("count"); W.Value(Stat.Count);
        W.EndObject();
    }
    W.EndArray();
    W.EndObject();
    return W.ToString();
}

void MRouterServer::HandlePacket(uint64 ConnectionId, const TByteArray& Data)
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

    const uint8 PacketType = Data[0];
    const TByteArray Payload(Data.begin() + 1, Data.end());
    if (PacketType == static_cast<uint8>(EServerMessageType::MT_RPC))
    {
        const uint16 HandshakeFunctionId = MGET_STABLE_RPC_FUNCTION_ID("MRouterServer", "Rpc_OnServerHandshake");
        const bool bAuthenticated = PeerIt->second.bAuthenticated;
        if (!bAuthenticated && PeekServerRpcFunctionId(Payload) != HandshakeFunctionId)
        {
            LOG_WARN("Router rejecting non-handshake MT_RPC from unauthenticated connection %llu",
                     static_cast<unsigned long long>(ConnectionId));
            return;
        }

        if (!TryInvokeServerRpc(this, ConnectionId, Payload, ERpcType::ServerToServer))
        {
            LOG_WARN("Router MT_RPC packet could not be handled via reflection (connection=%llu)",
                     static_cast<unsigned long long>(ConnectionId));
        }
        return;
    }

    LOG_WARN("Router received unexpected non-RPC peer message type %u from connection %llu",
             static_cast<unsigned>(PacketType),
             static_cast<unsigned long long>(ConnectionId));
}

void MRouterServer::Rpc_OnServerHandshake(uint32 ServerId, uint8 ServerTypeValue, const MString& ServerName)
{
    const uint64 ConnectionId = GetCurrentServerRpcConnectionId();
    auto PeerIt = Peers.find(ConnectionId);
    if (PeerIt == Peers.end())
    {
        return;
    }

    SRouterPeer& Peer = PeerIt->second;
    Peer.ServerId = ServerId;
    Peer.ServerType = static_cast<EServerType>(ServerTypeValue);
    Peer.ServerName = ServerName;
    Peer.bAuthenticated = true;

    LOG_INFO("Router authenticated %s (id=%u type=%d)",
             Peer.ServerName.c_str(), Peer.ServerId, (int)Peer.ServerType);
}

void MRouterServer::Rpc_OnHeartbeat(uint32 Sequence)
{
    LOG_DEBUG("Router heartbeat received (connection=%llu seq=%u)",
              static_cast<unsigned long long>(GetCurrentServerRpcConnectionId()),
              static_cast<unsigned>(Sequence));
}

void MRouterServer::Rpc_OnPeerServerRegister(
    uint32 ServerId,
    uint8 ServerTypeValue,
    const MString& ServerName,
    const MString& Address,
    uint16 Port,
    uint16 ZoneId)
{
    const EServerType ServerType = static_cast<EServerType>(ServerTypeValue);
    auto PeerIt = std::find_if(
        Peers.begin(),
        Peers.end(),
        [ServerId, ServerType](auto& Entry)
        {
            return Entry.second.bAuthenticated &&
                   Entry.second.ServerId == ServerId &&
                   Entry.second.ServerType == ServerType;
        });
    if (PeerIt == Peers.end())
    {
        return;
    }

    SRouterPeer& Peer = PeerIt->second;
    Peer.ServerId = ServerId;
    Peer.ServerType = ServerType;
    Peer.ServerName = ServerName;
    Peer.Address = Address;
    Peer.Port = Port;
    Peer.ZoneId = ZoneId;
    Peer.bRegistered = true;

    if (!MRpc::Call(
            Peer.Connection,
            Peer.ServerType,
            "Rpc_OnRouterServerRegisterAck",
            static_cast<uint8>(1)))
    {
        LOG_WARN("Router->%s register ack RPC send failed (server=%u)",
                 Peer.ServerName.c_str(),
                 Peer.ServerId);
    }

    LOG_INFO("Registered server %s (id=%u type=%d addr=%s:%u)",
             Peer.ServerName.c_str(),
             Peer.ServerId,
             (int)Peer.ServerType,
             Peer.Address.c_str(),
             Peer.Port);
}

void MRouterServer::Rpc_OnPeerServerLoadReport(uint32 ServerId, uint32 CurrentLoad, uint32 Capacity)
{
    const uint64 ConnectionId = GetCurrentServerRpcConnectionId();
    auto PeerIt = Peers.find(ConnectionId);
    if (PeerIt == Peers.end())
    {
        return;
    }

    SRouterPeer& Peer = PeerIt->second;
    if (!Peer.bAuthenticated || !Peer.bRegistered)
    {
        return;
    }
    if (Peer.ServerId != ServerId)
    {
        LOG_WARN("Router load report server id mismatch: conn=%llu expected=%u got=%u",
                 static_cast<unsigned long long>(ConnectionId),
                 Peer.ServerId,
                 ServerId);
        return;
    }
    Peer.CurrentLoad = CurrentLoad;
    Peer.Capacity = (Capacity > 0) ? Capacity : 1;
}

void MRouterServer::Rpc_OnPeerRouteQuery(
    uint32 ServerId,
    uint64 RequestId,
    uint8 RequestedTypeValue,
    uint64 PlayerId,
    uint16 ZoneId)
{
    const uint64 ConnectionId = GetCurrentServerRpcConnectionId();
    auto PeerIt = Peers.find(ConnectionId);
    if (PeerIt == Peers.end())
    {
        return;
    }

    const SRouterPeer& Peer = PeerIt->second;
    if (!Peer.bAuthenticated || !Peer.bRegistered)
    {
        return;
    }
    if (Peer.ServerId != ServerId)
    {
        LOG_WARN("Router route query server id mismatch: conn=%llu expected=%u got=%u",
                 static_cast<unsigned long long>(ConnectionId),
                 Peer.ServerId,
                 ServerId);
        return;
    }
    const EServerType RequestedType = static_cast<EServerType>(RequestedTypeValue);
    const SRouterPeer* Target = SelectRouteTarget(RequestedType, PlayerId, ZoneId);

    SRouteResponseMessage Response;
    Response.RequestId = RequestId;
    Response.RequestedType = RequestedType;
    Response.PlayerId = PlayerId;
    Response.bFound = (Target != nullptr);
    if (Target)
    {
        Response.ServerInfo = SServerInfo(Target->ServerId, Target->ServerType, Target->ServerName, Target->Address, Target->Port, Target->ZoneId);
    }

    if (!MRpc::Call(
            Peer.Connection,
            Peer.ServerType,
            "Rpc_OnRouterRouteResponse",
            Response.RequestId,
            static_cast<uint8>(Response.RequestedType),
            Response.PlayerId,
            Response.bFound,
            Response.ServerInfo.ServerId,
            static_cast<uint8>(Response.ServerInfo.ServerType),
            Response.ServerInfo.ServerName,
            Response.ServerInfo.Address,
            Response.ServerInfo.Port,
            Response.ServerInfo.ZoneId))
    {
        LOG_WARN("Router->%s route response RPC send failed (request=%llu)",
                 Peer.ServerName.c_str(),
                 static_cast<unsigned long long>(Response.RequestId));
    }

    LOG_INFO("Route query from %s for type=%d player=%llu result=%s",
             Peer.ServerName.c_str(),
             (int)RequestedType,
             (unsigned long long)PlayerId,
             Target ? Target->ServerName.c_str() : "none");
}

bool MRouterServer::SendServerPacket(uint64 ConnectionId, uint8 PacketType, const TByteArray& Payload)
{
    auto It = Peers.find(ConnectionId);
    if (It == Peers.end() || !It->second.Connection)
    {
        return false;
    }

    TByteArray Packet;
    Packet.reserve(1 + Payload.size());
    Packet.push_back(PacketType);
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
