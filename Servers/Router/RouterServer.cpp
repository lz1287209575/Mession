#include "RouterServer.h"
#include <poll.h>

namespace
{
template<typename T>
void AppendValue(TArray& OutData, const T& Value)
{
    const auto* ValueBytes = reinterpret_cast<const uint8*>(&Value);
    OutData.insert(OutData.end(), ValueBytes, ValueBytes + sizeof(T));
}

template<typename T>
bool ReadValue(const TArray& Data, size_t& Offset, T& OutValue)
{
    if (Offset + sizeof(T) > Data.size())
        return false;

    memcpy(&OutValue, Data.data() + Offset, sizeof(T));
    Offset += sizeof(T);
    return true;
}

void AppendString(TArray& OutData, const FString& Value)
{
    const uint16 Length = static_cast<uint16>(Value.size());
    AppendValue(OutData, Length);
    OutData.insert(OutData.end(), Value.begin(), Value.end());
}

bool ReadString(const TArray& Data, size_t& Offset, FString& OutValue)
{
    uint16 Length = 0;
    if (!ReadValue(Data, Offset, Length) || Offset + Length > Data.size())
        return false;

    OutValue.assign(reinterpret_cast<const char*>(Data.data() + Offset), Length);
    Offset += Length;
    return true;
}
}

bool MRouterServer::Init(int InPort)
{
    Config.ListenPort = static_cast<uint16>(InPort);
    ListenSocket = MSocket::CreateListenSocket(Config.ListenPort);
    if (ListenSocket < 0)
    {
        LOG_ERROR("Failed to create router listen socket on port %d", InPort);
        return false;
    }

    bRunning = true;

    printf("=====================================\n");
    printf("  Mession Router Server\n");
    printf("  Listening on port %d (fd=%d)\n", InPort, ListenSocket);
    printf("=====================================\n");

    return true;
}

void MRouterServer::Shutdown()
{
    if (!bRunning)
        return;

    bRunning = false;

    for (auto& [ConnectionId, Peer] : Peers)
    {
        if (Peer.Connection)
            Peer.Connection->Close();
    }
    Peers.clear();

    if (ListenSocket >= 0)
    {
        MSocket::Close(ListenSocket);
        ListenSocket = -1;
    }

    LOG_INFO("Router server shutdown complete");
}

void MRouterServer::Tick()
{
    if (!bRunning)
        return;

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
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

void MRouterServer::AcceptServers()
{
    TString Address;
    uint16 Port = 0;
    int32 ClientSocket = MSocket::Accept(ListenSocket, Address, Port);

    while (ClientSocket >= 0)
    {
        const uint64 ConnectionId = NextConnectionId++;
        auto Connection = TSharedPtr<MTcpConnection>(new MTcpConnection(ClientSocket));
        Connection->SetNonBlocking(true);

        SRouterPeer Peer;
        Peer.Connection = Connection;
        Peer.Address = Address;
        Peers[ConnectionId] = Peer;

        LOG_INFO("New router peer connected: %s:%d (connection_id=%llu)",
                 Address.c_str(), Port, (unsigned long long)ConnectionId);

        ClientSocket = MSocket::Accept(ListenSocket, Address, Port);
    }
}

void MRouterServer::ProcessMessages()
{
    TVector<uint64> DisconnectedConnections;
    TVector<pollfd> PollFds;

    for (auto& [ConnectionId, Peer] : Peers)
    {
        if (Peer.Connection && Peer.Connection->IsConnected())
        {
            Peer.Connection->FlushSendBuffer();
            pollfd Pfd;
            Pfd.fd = Peer.Connection->GetSocketFd();
            Pfd.events = POLLIN;
            Pfd.revents = 0;
            PollFds.push_back(Pfd);
        }
    }

    if (PollFds.empty())
        return;

    const int32 Ret = poll(PollFds.data(), PollFds.size(), 10);
    if (Ret < 0)
        return;

    size_t Index = 0;
    for (auto& [ConnectionId, Peer] : Peers)
    {
        if (Index >= PollFds.size())
            break;

        if (PollFds[Index].revents & POLLIN)
        {
            TArray Packet;
            while (Peer.Connection->ReceivePacket(Packet))
            {
                HandlePacket(ConnectionId, Packet);
            }

            if (!Peer.Connection->IsConnected())
                DisconnectedConnections.push_back(ConnectionId);
        }

        ++Index;
    }

    for (uint64 ConnectionId : DisconnectedConnections)
    {
        RemovePeer(ConnectionId);
    }
}

void MRouterServer::HandlePacket(uint64 ConnectionId, const TArray& Data)
{
    if (Data.empty())
        return;

    auto PeerIt = Peers.find(ConnectionId);
    if (PeerIt == Peers.end())
        return;

    SRouterPeer& Peer = PeerIt->second;
    const uint8 MsgType = Data[0];
    const TArray Payload(Data.begin() + 1, Data.end());

    switch ((EServerMessageType)MsgType)
    {
        case EServerMessageType::MT_ServerHandshake:
        {
            size_t Offset = 0;
            uint32 ServerId = 0;
            uint8 ServerTypeValue = 0;
            FString ServerName;
            if (!ReadValue(Payload, Offset, ServerId) ||
                !ReadValue(Payload, Offset, ServerTypeValue) ||
                !ReadString(Payload, Offset, ServerName))
            {
                LOG_WARN("Invalid router handshake payload from connection %llu",
                         (unsigned long long)ConnectionId);
                return;
            }

            Peer.ServerId = ServerId;
            Peer.ServerType = (EServerType)ServerTypeValue;
            Peer.ServerName = ServerName;
            Peer.bAuthenticated = true;

            SendServerMessage(ConnectionId, (uint8)EServerMessageType::MT_ServerHandshakeAck, {});
            LOG_INFO("Router authenticated %s (id=%u type=%d)",
                     Peer.ServerName.c_str(), Peer.ServerId, (int)Peer.ServerType);
            break;
        }

        case EServerMessageType::MT_Heartbeat:
        {
            SendServerMessage(ConnectionId, (uint8)EServerMessageType::MT_HeartbeatAck, {});
            break;
        }

        case EServerMessageType::MT_ServerRegister:
        {
            if (!Peer.bAuthenticated)
                return;

            size_t Offset = 0;
            uint32 ServerId = 0;
            uint8 ServerTypeValue = 0;
            FString ServerName;
            FString Address;
            uint16 Port = 0;
            if (!ReadValue(Payload, Offset, ServerId) ||
                !ReadValue(Payload, Offset, ServerTypeValue) ||
                !ReadString(Payload, Offset, ServerName) ||
                !ReadString(Payload, Offset, Address) ||
                !ReadValue(Payload, Offset, Port))
            {
                LOG_WARN("Invalid server register payload from connection %llu",
                         (unsigned long long)ConnectionId);
                return;
            }

            Peer.ServerId = ServerId;
            Peer.ServerType = (EServerType)ServerTypeValue;
            Peer.ServerName = ServerName;
            Peer.Address = Address;
            Peer.Port = Port;
            Peer.bRegistered = true;

            TArray AckPayload;
            AckPayload.push_back(1);
            SendServerMessage(ConnectionId, (uint8)EServerMessageType::MT_ServerRegisterAck, AckPayload);

            LOG_INFO("Registered server %s (id=%u type=%d addr=%s:%u)",
                     Peer.ServerName.c_str(),
                     Peer.ServerId,
                     (int)Peer.ServerType,
                     Peer.Address.c_str(),
                     Peer.Port);
            break;
        }

        case EServerMessageType::MT_RouteQuery:
        {
            if (!Peer.bAuthenticated)
                return;

            size_t Offset = 0;
            uint64 RequestId = 0;
            uint8 RequestedTypeValue = 0;
            uint64 PlayerId = 0;
            if (!ReadValue(Payload, Offset, RequestId) ||
                !ReadValue(Payload, Offset, RequestedTypeValue) ||
                !ReadValue(Payload, Offset, PlayerId))
            {
                LOG_WARN("Invalid route query payload from connection %llu",
                         (unsigned long long)ConnectionId);
                return;
            }

            const EServerType RequestedType = (EServerType)RequestedTypeValue;
            const SRouterPeer* Target = SelectRouteTarget(RequestedType, PlayerId);

            TArray ResponsePayload;
            AppendValue(ResponsePayload, RequestId);
            AppendValue(ResponsePayload, RequestedTypeValue);
            AppendValue(ResponsePayload, PlayerId);
            ResponsePayload.push_back(Target ? 1 : 0);

            if (Target)
            {
                AppendValue(ResponsePayload, Target->ServerId);
                ResponsePayload.push_back((uint8)Target->ServerType);
                AppendString(ResponsePayload, Target->ServerName);
                AppendString(ResponsePayload, Target->Address);
                AppendValue(ResponsePayload, Target->Port);
            }

            SendServerMessage(ConnectionId, (uint8)EServerMessageType::MT_RouteResponse, ResponsePayload);

            LOG_INFO("Route query from %s for type=%d player=%llu result=%s",
                     Peer.ServerName.c_str(),
                     (int)RequestedType,
                     (unsigned long long)PlayerId,
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
        return false;

    TArray Packet;
    Packet.reserve(1 + Payload.size());
    Packet.push_back(Type);
    Packet.insert(Packet.end(), Payload.begin(), Payload.end());
    return It->second.Connection->Send(Packet.data(), Packet.size());
}

const SRouterPeer* MRouterServer::SelectRouteTarget(EServerType RequestedType, uint64 PlayerId)
{
    if (RequestedType == EServerType::World && PlayerId != 0)
    {
        auto BindingIt = PlayerRouteBindings.find(PlayerId);
        if (BindingIt != PlayerRouteBindings.end())
        {
            const SRouterPeer* BoundServer = FindRegisteredServerById(BindingIt->second.WorldServerId);
            if (BoundServer)
                return BoundServer;

            PlayerRouteBindings.erase(BindingIt);
        }
    }

    const SRouterPeer* Selected = nullptr;
    for (const auto& [ConnectionId, Peer] : Peers)
    {
        (void)ConnectionId;
        if (!Peer.bRegistered || !Peer.Connection || !Peer.Connection->IsConnected())
            continue;
        if (Peer.ServerType != RequestedType)
            continue;

        if (!Selected || Peer.ServerId < Selected->ServerId)
            Selected = &Peer;
    }

    if (Selected && RequestedType == EServerType::World && PlayerId != 0)
    {
        PlayerRouteBindings[PlayerId] = {PlayerId, Selected->ServerId};
    }

    return Selected;
}

const SRouterPeer* MRouterServer::FindRegisteredServerById(uint32 ServerId) const
{
    for (const auto& [ConnectionId, Peer] : Peers)
    {
        (void)ConnectionId;
        if (!Peer.bRegistered || !Peer.Connection || !Peer.Connection->IsConnected())
            continue;
        if (Peer.ServerId == ServerId)
            return &Peer;
    }

    return nullptr;
}

void MRouterServer::RemovePeer(uint64 ConnectionId)
{
    auto It = Peers.find(ConnectionId);
    if (It == Peers.end())
        return;

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
                PlayersToUnbind.push_back(PlayerId);
        }

        for (uint64 PlayerId : PlayersToUnbind)
            PlayerRouteBindings.erase(PlayerId);
    }
}
