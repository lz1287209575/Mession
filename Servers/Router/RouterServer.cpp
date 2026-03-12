#include "RouterServer.h"
#include "Common/Config.h"
#include "Core/Poll.h"

namespace
{
const TMap<FString, const char*> RouterEnvMap = {
    {"port", "MESSION_ROUTER_PORT"},
    {"route_lease_seconds", "MESSION_ROUTE_LEASE_SECONDS"},
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
    return true;
}

bool MRouterServer::Init(int InPort)
{
    if (InPort > 0)
    {
        Config.ListenPort = static_cast<uint16>(InPort);
    }
    ListenSocket.Reset(MSocket::CreateListenSocket(static_cast<uint16>(Config.ListenPort)));
    if (!ListenSocket.IsValid())
    {
        LOG_ERROR("Failed to create router listen socket on port %d", Config.ListenPort);
        return false;
    }

    bRunning = true;

    MLogger::LogStartupBanner("RouterServer", Config.ListenPort, static_cast<intptr_t>(ListenSocket.Get()));

    return true;
}

void MRouterServer::RequestShutdown()
{
    bRunning = false;
    if (ListenSocket.IsValid())
    {
        ListenSocket.Reset();
    }
}

void MRouterServer::Shutdown()
{
    if (bShutdownDone)
    {
        return;
    }
    bShutdownDone = true;
    bRunning = false;

    for (auto& [ConnectionId, Peer] : Peers)
    {
        if (Peer.Connection)
        {
            Peer.Connection->Close();
        }
    }
    Peers.clear();

    if (ListenSocket.IsValid())
    {
        ListenSocket.Reset();
    }

    LOG_INFO("Router server shutdown complete");
}

void MRouterServer::Tick()
{
    if (!bRunning)
    {
        return;
    }

    ++TickCounter;
    AcceptServers();
    ProcessMessages();
}

void MRouterServer::Run()
{
    if (!bRunning)
    {
        LOG_ERROR("Router server not initialized!");
        return;
    }

    LOG_INFO("Router server running...");
    while (bRunning)
    {
        Tick();
        MTime::SleepMilliseconds(16);
    }
}

void MRouterServer::AcceptServers()
{
    SAcceptedSocket Accepted = MSocket::AcceptConnection(ListenSocket.Get());

    while (Accepted.IsValid())
    {
        const uint64 ConnectionId = NextConnectionId++;
        TSharedPtr<INetConnection> Connection = MakeShared<MTcpConnection>(
            std::move(Accepted.Socket),
            Accepted.RemoteAddress,
            Accepted.RemotePort);
        Connection->SetNonBlocking(true);

        SRouterPeer Peer;
        Peer.Connection = Connection;
        Peer.Address = Accepted.RemoteAddress;
        Peers[ConnectionId] = Peer;

        LOG_INFO("New router peer connected: %s:%d (connection_id=%llu)",
                 Accepted.RemoteAddress.c_str(), Accepted.RemotePort, (unsigned long long)ConnectionId);

        Accepted = MSocket::AcceptConnection(ListenSocket.Get());
    }
}

void MRouterServer::ProcessMessages()
{
    TVector<uint64> DisconnectedConnections;
    TVector<SSocketPollItem> PollItems = MSocketPoller::BuildReadableItems(
        Peers,
        [](SRouterPeer& Peer) -> INetConnection*
        {
            return Peer.Connection ? Peer.Connection.get() : nullptr;
        });

    if (PollItems.empty())
    {
        return;
    }

    TVector<SSocketPollResult> PollResults;
    const int32 Ret = MSocketPoller::PollReadable(PollItems, PollResults, 10);
    if (Ret < 0)
    {
        return;
    }

    for (const SSocketPollResult& PollResult : PollResults)
    {
        auto PeerIt = Peers.find(PollResult.ConnectionId);
        if (PeerIt == Peers.end())
        {
            continue;
        }

        SRouterPeer& Peer = PeerIt->second;
        if (MSocketPoller::IsReadable(PollResult))
        {
            TArray Packet;
            while (Peer.Connection->ReceivePacket(Packet))
            {
                HandlePacket(PollResult.ConnectionId, Packet);
            }

            if (!Peer.Connection->IsConnected())
            {
                DisconnectedConnections.push_back(PollResult.ConnectionId);
            }
        }
        else if (MSocketPoller::HasError(PollResult))
        {
            DisconnectedConnections.push_back(PollResult.ConnectionId);
        }
    }

    for (uint64 ConnectionId : DisconnectedConnections)
    {
        RemovePeer(ConnectionId);
    }
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

    SRouterPeer& Peer = PeerIt->second;
    const uint8 MsgType = Data[0];
    const TArray Payload(Data.begin() + 1, Data.end());

    switch ((EServerMessageType)MsgType)
    {
        case EServerMessageType::MT_ServerHandshake:
        {
            SServerHandshakeMessage Message;
            auto ParseResult = ParsePayload(Payload, Message, "handshake");
            if (!ParseResult.IsOk())
            {
                LOG_WARN("ParsePayload failed: %s (connection %llu)", ParseResult.GetError().c_str(), (unsigned long long)ConnectionId);
                return;
            }

            Peer.ServerId = Message.ServerId;
            Peer.ServerType = Message.ServerType;
            Peer.ServerName = Message.ServerName;
            Peer.bAuthenticated = true;

            SendServerMessage(ConnectionId, EServerMessageType::MT_ServerHandshakeAck, SEmptyServerMessage{});
            LOG_INFO("Router authenticated %s (id=%u type=%d)",
                     Peer.ServerName.c_str(), Peer.ServerId, (int)Peer.ServerType);
            break;
        }

        case EServerMessageType::MT_Heartbeat:
        {
            SendServerMessage(ConnectionId, EServerMessageType::MT_HeartbeatAck, SEmptyServerMessage{});
            break;
        }

        case EServerMessageType::MT_ServerRegister:
        {
            if (!Peer.bAuthenticated)
            {
                return;
            }

            SServerRegisterMessage Message;
            auto ParseResult = ParsePayload(Payload, Message, "MT_ServerRegister");
            if (!ParseResult.IsOk())
            {
                LOG_WARN("ParsePayload failed: %s (connection %llu)", ParseResult.GetError().c_str(), (unsigned long long)ConnectionId);
                return;
            }

            Peer.ServerId = Message.ServerId;
            Peer.ServerType = Message.ServerType;
            Peer.ServerName = Message.ServerName;
            Peer.Address = Message.Address;
            Peer.Port = Message.Port;
            Peer.ZoneId = Message.ZoneId;
            Peer.bRegistered = true;

            SendServerMessage(ConnectionId, EServerMessageType::MT_ServerRegisterAck, SServerRegisterAckMessage{1});

            LOG_INFO("Registered server %s (id=%u type=%d addr=%s:%u)",
                     Peer.ServerName.c_str(),
                     Peer.ServerId,
                     (int)Peer.ServerType,
                     Peer.Address.c_str(),
                     Peer.Port);
            break;
        }

        case EServerMessageType::MT_ServerLoadReport:
        {
            if (!Peer.bAuthenticated)
            {
                return;
            }

            SServerLoadReportMessage Message;
            auto ParseResult = ParsePayload(Payload, Message, "MT_ServerLoadReport");
            if (!ParseResult.IsOk())
            {
                LOG_WARN("ParsePayload failed: %s (connection %llu)", ParseResult.GetError().c_str(), (unsigned long long)ConnectionId);
                return;
            }

            Peer.CurrentLoad = Message.CurrentLoad;
            Peer.Capacity = (Message.Capacity > 0) ? Message.Capacity : 1;
            break;
        }

        case EServerMessageType::MT_RouteQuery:
        {
            if (!Peer.bAuthenticated)
            {
                return;
            }

            SRouteQueryMessage Query;
            auto ParseResult = ParsePayload(Payload, Query, "MT_RouteQuery");
            if (!ParseResult.IsOk())
            {
                LOG_WARN("ParsePayload failed: %s (connection %llu)", ParseResult.GetError().c_str(), (unsigned long long)ConnectionId);
                return;
            }

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

            SendServerMessage(ConnectionId, EServerMessageType::MT_RouteResponse, Response);

            LOG_INFO("Route query from %s for type=%d player=%llu result=%s",
                     Peer.ServerName.c_str(),
                     (int)Query.RequestedType,
                     (unsigned long long)Query.PlayerId,
                     Target ? Target->ServerName.c_str() : "none");
            break;
        }

        default:
            break;
    }
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
