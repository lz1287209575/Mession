#include "WorldServer.h"
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
}

MWorldServer::MWorldServer()
{
    ReplicationDriver = new MReplicationDriver();
}

bool MWorldServer::Init(int InPort)
{
    // 创建监听socket
    ListenSocket = MSocket::CreateListenSocket((uint16)InPort);
    if (ListenSocket < 0)
    {
        printf("ERROR: Failed to create listen socket on port %d\n", InPort);
        return false;
    }
    
    bRunning = true;
    
    printf("=====================================\n");
    printf("  Mession World Server\n");
    printf("  Listening on port %d (fd=%d)\n", InPort, ListenSocket);
    printf("=====================================\n");
    LOG_INFO("  Listening on port %d", Config.ListenPort);
    LOG_INFO("=====================================");
    
    return true;
}

void MWorldServer::Shutdown()
{
    if (!bRunning)
        return;
    
    bRunning = false;
    
    // 关闭所有连接
    for (auto& [Id, Peer] : BackendConnections)
    {
        if (Peer.Connection)
            Peer.Connection->Close();
    }
    BackendConnections.clear();
    
    // 清理玩家
    Players.clear();
    ConnectionToPlayer.clear();
    
    // 清理复制系统
    delete ReplicationDriver;
    ReplicationDriver = nullptr;
    
    // 关闭监听socket
    if (ListenSocket >= 0)
    {
        MSocket::Close(ListenSocket);
        ListenSocket = -1;
    }
    
    LOG_INFO("World server shutdown complete");
}

void MWorldServer::Tick()
{
    if (!bRunning)
        return;
    
    // 接受新连接
    AcceptConnections();
    
    // 处理消息
    ProcessMessages();
    
    // 游戏逻辑更新
    UpdateGameLogic(DEFAULT_TICK_RATE);
    
    // 复制更新
    if (ReplicationDriver)
    {
        ReplicationDriver->Tick(DEFAULT_TICK_RATE);
    }
}

void MWorldServer::Run()
{
    if (!bRunning)
    {
        LOG_ERROR("World server not initialized!");
        return;
    }
    
    LOG_INFO("World server running...");
    
    while (bRunning)
    {
        Tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

void MWorldServer::AcceptConnections()
{
    TString Address;
    uint16 Port;
    
    int32 ClientSocket = MSocket::Accept(ListenSocket, Address, Port);
    
    while (ClientSocket >= 0)
    {
        uint64 ConnectionId = NextConnectionId++;
        auto Connection = TSharedPtr<MTcpConnection>(new MTcpConnection(ClientSocket));
        Connection->SetNonBlocking(true);

        SBackendPeer Peer;
        Peer.Connection = Connection;
        BackendConnections[ConnectionId] = Peer;
        
        LOG_INFO("New connection: %s (connection_id=%llu)", 
                 Address.c_str(), (unsigned long long)ConnectionId);
        
        ClientSocket = MSocket::Accept(ListenSocket, Address, Port);
    }
}

void MWorldServer::ProcessMessages()
{
    TVector<uint64> DisconnectedConnections;
    
    TVector<pollfd> PollFds;
    for (auto& [ConnId, Peer] : BackendConnections)
    {
        if (Peer.Connection && Peer.Connection->IsConnected())
        {
            Peer.Connection->FlushSendBuffer();
            pollfd Pfd;
            Pfd.fd = Peer.Connection->GetSocketFd();
            Pfd.events = POLLIN;
            PollFds.push_back(Pfd);
        }
    }
    
    if (PollFds.empty())
        return;
    
    int32 Ret = poll(PollFds.data(), PollFds.size(), 10);
    
    if (Ret < 0)
        return;
    
    size_t Index = 0;
    
    for (auto& [ConnId, Peer] : BackendConnections)
    {
        if (Index >= PollFds.size())
            break;
        
        if (PollFds[Index].revents & POLLIN)
        {
            TArray Packet;
            while (Peer.Connection->ReceivePacket(Packet))
            {
                HandlePacket(ConnId, Packet);
            }
            
            if (!Peer.Connection->IsConnected())
            {
                DisconnectedConnections.push_back(ConnId);
            }
        }
        
        Index++;
    }
    
    // 处理断开的连接
    for (uint64 ConnId : DisconnectedConnections)
    {
        auto* Player = GetPlayerByConnection(ConnId);
        if (Player)
        {
            RemovePlayer(Player->PlayerId);
        }
        
        LOG_INFO("Connection disconnected: %llu", (unsigned long long)ConnId);
        BackendConnections.erase(ConnId);
    }
}

void MWorldServer::HandlePacket(uint64 ConnectionId, const TArray& Data)
{
    if (Data.empty())
        return;

    auto PeerIt = BackendConnections.find(ConnectionId);
    if (PeerIt == BackendConnections.end())
        return;

    SBackendPeer& Peer = PeerIt->second;
    const uint8 MsgType = Data[0];
    const TArray Payload(Data.begin() + 1, Data.end());

    switch ((EServerMessageType)MsgType)
    {
        case EServerMessageType::MT_ServerHandshake:
        {
            size_t Offset = 0;
            uint32 ServerId = 0;
            uint8 ServerTypeValue = 0;
            uint16 NameLen = 0;
            if (!ReadValue(Payload, Offset, ServerId) ||
                !ReadValue(Payload, Offset, ServerTypeValue) ||
                !ReadValue(Payload, Offset, NameLen) ||
                Offset + NameLen > Payload.size())
            {
                LOG_WARN("Invalid handshake payload from connection %llu",
                         (unsigned long long)ConnectionId);
                return;
            }

            Peer.ServerId = ServerId;
            Peer.ServerType = (EServerType)ServerTypeValue;
            Peer.ServerName.assign(reinterpret_cast<const char*>(Payload.data() + Offset), NameLen);
            Peer.bAuthenticated = true;

            SendServerMessage(ConnectionId, (uint8)EServerMessageType::MT_ServerHandshakeAck, {});
            LOG_INFO("%s authenticated as %d", Peer.ServerName.c_str(), (int)Peer.ServerType);
            break;
        }

        case EServerMessageType::MT_Heartbeat:
        {
            SendServerMessage(ConnectionId, (uint8)EServerMessageType::MT_HeartbeatAck, {});
            break;
        }

        case EServerMessageType::MT_PlayerLogin:
        {
            if (!Peer.bAuthenticated || Peer.ServerType != EServerType::Gateway)
                return;

            size_t Offset = 0;
            uint64 ClientConnectionId = 0;
            uint64 PlayerId = 0;
            uint32 SessionKey = 0;
            if (!ReadValue(Payload, Offset, ClientConnectionId) ||
                !ReadValue(Payload, Offset, PlayerId) ||
                !ReadValue(Payload, Offset, SessionKey))
            {
                LOG_WARN("Invalid player login payload size: %zu", Payload.size());
                return;
            }

            AddPlayer(PlayerId, "Player" + std::to_string(PlayerId), ClientConnectionId);
            auto* Player = GetPlayerById(PlayerId);
            if (Player)
                Player->SessionKey = SessionKey;
            break;
        }

        case EServerMessageType::MT_PlayerLogout:
        {
            if (!Peer.bAuthenticated || Peer.ServerType != EServerType::Gateway)
                return;

            size_t Offset = 0;
            uint64 PlayerId = 0;
            if (!ReadValue(Payload, Offset, PlayerId))
                return;

            RemovePlayer(PlayerId);
            break;
        }

        case EServerMessageType::MT_PlayerDataSync:
        {
            if (!Peer.bAuthenticated || Peer.ServerType != EServerType::Gateway)
                return;

            size_t Offset = 0;
            uint64 ClientConnectionId = 0;
            uint32 DataSize = 0;
            if (!ReadValue(Payload, Offset, ClientConnectionId) ||
                !ReadValue(Payload, Offset, DataSize) ||
                Offset + DataSize > Payload.size())
            {
                LOG_WARN("Invalid gameplay payload size: %zu", Payload.size());
                return;
            }

            TArray GameplayData(Payload.begin() + Offset, Payload.begin() + Offset + DataSize);
            HandleGameplayPacket(ClientConnectionId, GameplayData);
            break;
        }

        default:
            break;
    }
}

void MWorldServer::HandleGameplayPacket(uint64 ConnectionId, const TArray& Data)
{
    if (Data.empty())
        return;

    const uint8 MsgType = Data[0];

    switch (MsgType)
    {
        case 5: // PlayerMove
        {
            if (Data.size() < 1 + sizeof(float) * 3)
                return;
            
            auto* Player = GetPlayerByConnection(ConnectionId);
            if (!Player || !Player->Character)
                return;
            
            SVector NewPos;
            memcpy(&NewPos.X, Data.data() + 1, sizeof(float));
            memcpy(&NewPos.Y, Data.data() + 1 + sizeof(float), sizeof(float));
            memcpy(&NewPos.Z, Data.data() + 1 + sizeof(float) * 2, sizeof(float));
            
            Player->Character->SetLocation(NewPos);

            TArray ScenePayload;
            AppendValue(ScenePayload, Player->PlayerId);
            AppendValue(ScenePayload, Player->CurrentSceneId);
            AppendValue(ScenePayload, NewPos.X);
            AppendValue(ScenePayload, NewPos.Y);
            AppendValue(ScenePayload, NewPos.Z);
            BroadcastToScenes((uint8)EServerMessageType::MT_PlayerDataSync, ScenePayload);
            
            LOG_DEBUG("Player %llu moved to (%.2f, %.2f, %.2f)",
                     (unsigned long long)Player->PlayerId, NewPos.X, NewPos.Y, NewPos.Z);
            break;
        }
        default:
            LOG_DEBUG("Unknown message type: %d", MsgType);
            break;
    }
}

void MWorldServer::AddPlayer(uint64 PlayerId, const FString& Name, uint64 ConnectionId)
{
    if (Players.find(PlayerId) != Players.end())
        return;

    SPlayer Player;
    Player.PlayerId = PlayerId;
    Player.Name = Name;
    Player.ConnectionId = ConnectionId;
    Player.CurrentSceneId = 1;
    Player.bOnline = true;
    
    // 创建角色
    MActor* Character = new MActor();
    Character->SetReplicated(true);
    Character->SetActorReplicates(true);
    Character->SetActorActive(true);
    Character->SetLocation(SVector(0, 0, 100));
    
    Player.Character = Character;
    
    Players[PlayerId] = Player;
    ConnectionToPlayer[ConnectionId] = PlayerId;
    
    // 注册到复制系统
    ReplicationDriver->RegisterActor(Character);
    ReplicationDriver->AddRelevantActor(ConnectionId, Character->GetObjectId());

    TArray ScenePayload;
    AppendValue(ScenePayload, Player.PlayerId);
    AppendValue(ScenePayload, Player.CurrentSceneId);
    AppendValue(ScenePayload, Player.Character->GetLocation().X);
    AppendValue(ScenePayload, Player.Character->GetLocation().Y);
    AppendValue(ScenePayload, Player.Character->GetLocation().Z);
    BroadcastToScenes((uint8)EServerMessageType::MT_PlayerSwitchServer, ScenePayload);
    
    LOG_INFO("Player %s (id=%llu) added to world", 
             Name.c_str(), (unsigned long long)PlayerId);
}

void MWorldServer::RemovePlayer(uint64 PlayerId)
{
    auto It = Players.find(PlayerId);
    if (It == Players.end())
        return;
    
    SPlayer& Player = It->second;
    
    // 移除复制
    if (Player.Character)
    {
        ReplicationDriver->BroadcastActorDestroy(Player.Character->GetObjectId());
        delete Player.Character;
    }

    TArray ScenePayload;
    AppendValue(ScenePayload, Player.PlayerId);
    AppendValue(ScenePayload, Player.CurrentSceneId);
    BroadcastToScenes((uint8)EServerMessageType::MT_PlayerLogout, ScenePayload);
    
    ConnectionToPlayer.erase(Player.ConnectionId);
    Players.erase(It);
    
    LOG_INFO("Player %llu removed from world", (unsigned long long)PlayerId);
}

SPlayer* MWorldServer::GetPlayerById(uint64 PlayerId)
{
    auto It = Players.find(PlayerId);
    return (It != Players.end()) ? &It->second : nullptr;
}

SPlayer* MWorldServer::GetPlayerByConnection(uint64 ConnectionId)
{
    auto It = ConnectionToPlayer.find(ConnectionId);
    if (It == ConnectionToPlayer.end())
        return nullptr;
    
    return GetPlayerById(It->second);
}

void MWorldServer::UpdateGameLogic(float DeltaTime)
{
    // 更新所有玩家角色
    for (auto& [PlayerId, Player] : Players)
    {
        if (Player.Character)
        {
            Player.Character->Tick(DeltaTime);
        }
    }
}

bool MWorldServer::SendServerMessage(uint64 ConnectionId, uint8 Type, const TArray& Payload)
{
    auto It = BackendConnections.find(ConnectionId);
    if (It == BackendConnections.end() || !It->second.Connection)
        return false;

    TArray Packet;
    Packet.reserve(1 + Payload.size());
    Packet.push_back(Type);
    Packet.insert(Packet.end(), Payload.begin(), Payload.end());
    return It->second.Connection->Send(Packet.data(), Packet.size());
}

void MWorldServer::BroadcastToScenes(uint8 Type, const TArray& Payload)
{
    for (auto& [ConnectionId, Peer] : BackendConnections)
    {
        if (!Peer.bAuthenticated || Peer.ServerType != EServerType::Scene || !Peer.Connection)
            continue;

        SendServerMessage(ConnectionId, Type, Payload);
    }
}
