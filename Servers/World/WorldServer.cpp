#include "WorldServer.h"
#include <poll.h>

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
    for (auto& [Id, Conn] : GatewayConnections)
    {
        Conn->Close();
    }
    GatewayConnections.clear();
    
    for (auto& [Id, Conn] : SceneServers)
    {
        Conn->Close();
    }
    SceneServers.clear();
    
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
        
        GatewayConnections[ConnectionId] = Connection;
        
        LOG_INFO("New connection: %s (connection_id=%llu)", 
                 Address.c_str(), (unsigned long long)ConnectionId);
        
        ClientSocket = MSocket::Accept(ListenSocket, Address, Port);
    }
}

void MWorldServer::ProcessMessages()
{
    TVector<uint64> DisconnectedConnections;
    
    TVector<pollfd> PollFds;
    for (auto& [ConnId, Conn] : GatewayConnections)
    {
        if (Conn->IsConnected())
        {
            Conn->FlushSendBuffer();
            pollfd Pfd;
            Pfd.fd = Conn->GetSocketFd();
            Pfd.events = POLLIN;
            PollFds.push_back(Pfd);
        }
    }
    
    for (auto& [SceneId, Conn] : SceneServers)
    {
        if (Conn->IsConnected())
        {
            Conn->FlushSendBuffer();
            pollfd Pfd;
            Pfd.fd = Conn->GetSocketFd();
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
    
    // 处理网关连接
    for (auto& [ConnId, Conn] : GatewayConnections)
    {
        if (Index >= PollFds.size())
            break;
        
        if (PollFds[Index].revents & POLLIN)
        {
            TArray Packet;
            while (Conn->ReceivePacket(Packet))
            {
                HandlePacket(ConnId, Packet);
            }
            
            if (!Conn->IsConnected())
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
        GatewayConnections.erase(ConnId);
    }
}

void MWorldServer::HandlePacket(uint64 ConnectionId, const TArray& Data)
{
    if (Data.empty())
        return;
    
    uint8 MsgType = Data[0];
    
    switch (MsgType)
    {
        case 5: // PlayerMove
        {
            if (Data.size() < 25)
                return;
            
            auto* Player = GetPlayerByConnection(ConnectionId);
            if (!Player || !Player->Character)
                return;
            
            SVector NewPos;
            NewPos.X = *(float*)&Data[1];
            NewPos.Y = *(float*)&Data[5];
            NewPos.Z = *(float*)&Data[9];
            
            Player->Character->SetLocation(NewPos);
            
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
    SPlayer Player;
    Player.PlayerId = PlayerId;
    Player.Name = Name;
    Player.ConnectionId = ConnectionId;
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
