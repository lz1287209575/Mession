#include "WorldServer.h"
#include "Messages/NetMessages.h"
#include "Core/Poll.h"

MWorldServer::MWorldServer()
{
    ReplicationDriver = new MReplicationDriver();
}

bool MWorldServer::Init(int InPort)
{
    Config.ListenPort = static_cast<uint16>(InPort);
    MServerConnection::SetLocalInfo(3, EServerType::World, Config.ServerName);

    // 创建监听socket
    ListenSocket = MSocket::CreateListenSocket((uint16)InPort);
    if (ListenSocket == INVALID_SOCKET_FD)
    {
        printf("ERROR: Failed to create listen socket on port %d\n", InPort);
        return false;
    }

    bRunning = true;

    printf("=====================================\n");
    printf("  Mession World Server\n");
    printf("  Listening on port %d (fd=%zd)\n", InPort, (intptr_t)ListenSocket);
    printf("=====================================\n");
    LOG_INFO("  Listening on port %d", Config.ListenPort);
    LOG_INFO("=====================================");

    SServerConnectionConfig RouterConfig(100, EServerType::Router, "Router01", Config.RouterServerAddr, Config.RouterServerPort);
    RouterServerConn = TSharedPtr<MServerConnection>(new MServerConnection(RouterConfig));
    RouterServerConn->SetOnAuthenticated([this](auto, const SServerInfo& Info) {
        LOG_INFO("Router server authenticated: %s", Info.ServerName.c_str());
        SendRouterRegister();
        QueryLoginServerRoute();
    });
    RouterServerConn->SetOnMessage([this](auto, uint8 Type, const TArray& Data) {
        HandleRouterServerMessage(Type, Data);
    });
    RouterServerConn->Connect();

    SServerConnectionConfig LoginConfig(2, EServerType::Login, "Login01", "", 0);
    LoginServerConn = TSharedPtr<MServerConnection>(new MServerConnection(LoginConfig));
    LoginServerConn->SetOnAuthenticated([](auto, const SServerInfo& Info) {
        LOG_INFO("Login server authenticated: %s", Info.ServerName.c_str());
    });
    LoginServerConn->SetOnMessage([this](auto, uint8 Type, const TArray& Data) {
        HandleLoginServerMessage(Type, Data);
    });
    
    return true;
}

void MWorldServer::Shutdown()
{
    if (!bRunning)
    {
        return;
    }
    
    bRunning = false;
    
    // 关闭所有连接
    for (auto& [Id, Peer] : BackendConnections)
    {
        if (Peer.Connection)
        {
            Peer.Connection->Close();
        }
    }
    BackendConnections.clear();

    if (RouterServerConn)
    {
        RouterServerConn->Disconnect();
    }
    if (LoginServerConn)
    {
        LoginServerConn->Disconnect();
    }
    PendingSessionValidations.clear();
    
    // 清理玩家
    Players.clear();
    ConnectionToPlayer.clear();
    
    // 清理复制系统
    delete ReplicationDriver;
    ReplicationDriver = nullptr;
    
    // 关闭监听socket
    if (ListenSocket != INVALID_SOCKET_FD)
    {
        MSocket::Close(ListenSocket);
        ListenSocket = INVALID_SOCKET_FD;
    }
    
    LOG_INFO("World server shutdown complete");
}

void MWorldServer::Tick()
{
    if (!bRunning)
    {
        return;
    }
    
    // 接受新连接
    AcceptConnections();

    if (RouterServerConn)
    {
        RouterServerConn->Tick(DEFAULT_TICK_RATE);
    }

    LoginRouteQueryTimer += DEFAULT_TICK_RATE;
    if (RouterServerConn && RouterServerConn->IsConnected() && LoginRouteQueryTimer >= 1.0f)
    {
        LoginRouteQueryTimer = 0.0f;
        if (!LoginServerConn || !LoginServerConn->IsConnected())
        {
            QueryLoginServerRoute();
        }
    }

    if (LoginServerConn)
    {
        LoginServerConn->Tick(DEFAULT_TICK_RATE);
    }
    
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
    
    TSocketFd ClientSocket = MSocket::Accept(ListenSocket, Address, Port);

    while (ClientSocket != INVALID_SOCKET_FD)
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
    {
        return;
    }
    
    int32 Ret = poll(PollFds.data(), PollFds.size(), 10);
    
    if (Ret < 0)
    {
        return;
    }
    
    size_t Index = 0;
    
    for (auto& [ConnId, Peer] : BackendConnections)
    {
        if (Index >= PollFds.size())
        {
            break;
        }
        
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
    {
        return;
    }

    auto PeerIt = BackendConnections.find(ConnectionId);
    if (PeerIt == BackendConnections.end())
    {
        return;
    }

    SBackendPeer& Peer = PeerIt->second;
    const uint8 MsgType = Data[0];
    const TArray Payload(Data.begin() + 1, Data.end());

    switch ((EServerMessageType)MsgType)
    {
        case EServerMessageType::MT_ServerHandshake:
        {
            SServerHandshakeMessage Message;
            if (!ParsePayload(Payload, Message))
            {
                LOG_WARN("Invalid handshake payload from connection %llu",
                         (unsigned long long)ConnectionId);
                return;
            }

            Peer.ServerId = Message.ServerId;
            Peer.ServerType = Message.ServerType;
            Peer.ServerName = Message.ServerName;
            Peer.bAuthenticated = true;

            SendServerMessage(ConnectionId, EServerMessageType::MT_ServerHandshakeAck, SEmptyServerMessage{});
            LOG_INFO("%s authenticated as %d", Peer.ServerName.c_str(), (int)Peer.ServerType);
            break;
        }

        case EServerMessageType::MT_Heartbeat:
        {
            SendServerMessage(ConnectionId, EServerMessageType::MT_HeartbeatAck, SEmptyServerMessage{});
            break;
        }

        case EServerMessageType::MT_PlayerLogin:
        {
            if (!Peer.bAuthenticated || Peer.ServerType != EServerType::Gateway)
            {
                return;
            }

            SPlayerLoginResponseMessage Message;
            if (!ParsePayload(Payload, Message))
            {
                LOG_WARN("Invalid player login payload size: %zu", Payload.size());
                return;
            }

            RequestSessionValidation(Message.ConnectionId, Message.PlayerId, Message.SessionKey);
            break;
        }

        case EServerMessageType::MT_PlayerLogout:
        {
            if (!Peer.bAuthenticated || Peer.ServerType != EServerType::Gateway)
            {
                return;
            }

            SPlayerLogoutMessage Message;
            if (!ParsePayload(Payload, Message))
            {
                return;
            }

            RemovePlayer(Message.PlayerId);
            break;
        }

        case EServerMessageType::MT_PlayerDataSync:
        {
            if (!Peer.bAuthenticated || Peer.ServerType != EServerType::Gateway)
            {
                return;
            }

            SGameplaySyncMessage Message;
            if (!ParsePayload(Payload, Message))
            {
                LOG_WARN("Invalid gameplay payload size: %zu", Payload.size());
                return;
            }

            HandleGameplayPacket(Message.ConnectionId, Message.Data);
            break;
        }

        default:
            break;
    }
}

void MWorldServer::HandleGameplayPacket(uint64 ConnectionId, const TArray& Data)
{
    if (Data.empty())
    {
        return;
    }

    const EClientMessageType MsgType = (EClientMessageType)Data[0];

    switch (MsgType)
    {
        case EClientMessageType::MT_PlayerMove:
        {
            if (Data.size() < 1 + sizeof(float) * 3)
            {
                return;
            }
            
            auto* Player = GetPlayerByConnection(ConnectionId);
            if (!Player || !Player->Character)
            {
                return;
            }
            
            SVector NewPos;
            memcpy(&NewPos.X, Data.data() + 1, sizeof(float));
            memcpy(&NewPos.Y, Data.data() + 1 + sizeof(float), sizeof(float));
            memcpy(&NewPos.Z, Data.data() + 1 + sizeof(float) * 2, sizeof(float));
            
            Player->Character->SetLocation(NewPos);

            const SPlayerSceneStateMessage Message{
                Player->PlayerId,
                static_cast<uint16>(Player->CurrentSceneId),
                NewPos.X,
                NewPos.Y,
                NewPos.Z
            };
            BroadcastToScenes((uint8)EServerMessageType::MT_PlayerDataSync, BuildPayload(Message));
            
            LOG_DEBUG("Player %llu moved to (%.2f, %.2f, %.2f)",
                     (unsigned long long)Player->PlayerId, NewPos.X, NewPos.Y, NewPos.Z);
            break;
        }
        default:
            LOG_DEBUG("Unknown message type: %d", (int)MsgType);
            break;
    }
}

void MWorldServer::AddPlayer(uint64 PlayerId, const FString& Name, uint64 ConnectionId)
{
    if (Players.find(PlayerId) != Players.end())
    {
        return;
    }

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

    const SPlayerSceneStateMessage Message{
        Player.PlayerId,
        static_cast<uint16>(Player.CurrentSceneId),
        Player.Character->GetLocation().X,
        Player.Character->GetLocation().Y,
        Player.Character->GetLocation().Z
    };
    BroadcastToScenes((uint8)EServerMessageType::MT_PlayerSwitchServer, BuildPayload(Message));
    
    LOG_INFO("Player %s (id=%llu) added to world", 
             Name.c_str(), (unsigned long long)PlayerId);
}

void MWorldServer::RemovePlayer(uint64 PlayerId)
{
    auto It = Players.find(PlayerId);
    if (It == Players.end())
    {
        return;
    }
    
    SPlayer& Player = It->second;
    
    // 移除复制
    if (Player.Character)
    {
        ReplicationDriver->BroadcastActorDestroy(Player.Character->GetObjectId());
        delete Player.Character;
    }

    BroadcastToScenes(
        (uint8)EServerMessageType::MT_PlayerLogout,
        BuildPayload(SPlayerSceneLeaveMessage{Player.PlayerId, static_cast<uint16>(Player.CurrentSceneId)}));
    
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
    {
        return nullptr;
    }
    
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
    {
        return false;
    }

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
        {
            continue;
        }

        SendServerMessage(ConnectionId, Type, Payload);
    }
}

void MWorldServer::HandleLoginServerMessage(uint8 Type, const TArray& Data)
{
    if (Type != (uint8)EServerMessageType::MT_SessionValidateResponse)
    {
        return;
    }

    SSessionValidateResponseMessage Message;
    if (!ParsePayload(Data, Message))
    {
        LOG_WARN("Invalid session validation response size: %zu", Data.size());
        return;
    }

    auto PendingIt = PendingSessionValidations.find(Message.ConnectionId);
    if (PendingIt == PendingSessionValidations.end())
    {
        return;
    }

    const SPendingSessionValidation Pending = PendingIt->second;
    PendingSessionValidations.erase(PendingIt);

    if (!Message.bValid || Pending.PlayerId != Message.PlayerId)
    {
        LOG_WARN("Session validation failed for player %llu on connection %llu",
                 (unsigned long long)Pending.PlayerId,
                 (unsigned long long)Message.ConnectionId);
        return;
    }

    AddPlayer(Message.PlayerId, "Player" + std::to_string(Message.PlayerId), Message.ConnectionId);
    auto* Player = GetPlayerById(Message.PlayerId);
    if (Player)
    {
        Player->SessionKey = Pending.SessionKey;
    }
}

void MWorldServer::RequestSessionValidation(uint64 ConnectionId, uint64 PlayerId, uint32 SessionKey)
{
    if (!LoginServerConn || !LoginServerConn->IsConnected())
    {
        LOG_WARN("Login server unavailable, cannot validate session for player %llu",
                 (unsigned long long)PlayerId);
        return;
    }

    PendingSessionValidations[ConnectionId] = {ConnectionId, PlayerId, SessionKey};

    SendTypedServerMessage(
        LoginServerConn,
        EServerMessageType::MT_SessionValidateRequest,
        SSessionValidateRequestMessage{ConnectionId, PlayerId, SessionKey});
}

void MWorldServer::HandleRouterServerMessage(uint8 Type, const TArray& Data)
{
    switch ((EServerMessageType)Type)
    {
        case EServerMessageType::MT_ServerRegisterAck:
            LOG_INFO("World server registered to RouterServer");
            break;

        case EServerMessageType::MT_RouteResponse:
        {
            SRouteResponseMessage Message;
            if (!ParsePayload(Data, Message))
            {
                LOG_WARN("Invalid router route response size: %zu", Data.size());
                return;
            }

            if (Message.PlayerId != 0 || Message.RequestedType != EServerType::Login || !Message.bFound)
            {
                return;
            }

            ApplyLoginServerRoute(
                Message.ServerInfo.ServerId,
                Message.ServerInfo.ServerName,
                Message.ServerInfo.Address,
                Message.ServerInfo.Port);
            break;
        }

        default:
            break;
    }
}

void MWorldServer::SendRouterRegister()
{
    if (!RouterServerConn || !RouterServerConn->IsConnected())
    {
        return;
    }

    SendTypedServerMessage(
        RouterServerConn,
        EServerMessageType::MT_ServerRegister,
        SServerRegisterMessage{3, EServerType::World, Config.ServerName, "127.0.0.1", Config.ListenPort});
}

void MWorldServer::QueryLoginServerRoute()
{
    if (!RouterServerConn || !RouterServerConn->IsConnected())
    {
        return;
    }

    SendTypedServerMessage(
        RouterServerConn,
        EServerMessageType::MT_RouteQuery,
        SRouteQueryMessage{NextRouteRequestId++, EServerType::Login, 0});
}

void MWorldServer::ApplyLoginServerRoute(uint32 ServerId, const FString& ServerName, const FString& Address, uint16 Port)
{
    if (!LoginServerConn)
    {
        return;
    }

    const SServerConnectionConfig& CurrentConfig = LoginServerConn->GetConfig();
    const bool bRouteChanged =
        CurrentConfig.ServerId != ServerId ||
        CurrentConfig.ServerName != ServerName ||
        CurrentConfig.Address != Address ||
        CurrentConfig.Port != Port;

    if (bRouteChanged && (LoginServerConn->IsConnected() || LoginServerConn->IsConnecting()))
    {
        LoginServerConn->Disconnect();
    }

    SServerConnectionConfig NewConfig(ServerId, EServerType::Login, ServerName, Address, Port);
    NewConfig.HeartbeatInterval = CurrentConfig.HeartbeatInterval;
    NewConfig.ConnectTimeout = CurrentConfig.ConnectTimeout;
    NewConfig.ReconnectInterval = CurrentConfig.ReconnectInterval;
    LoginServerConn->SetConfig(NewConfig);

    if (!LoginServerConn->IsConnected() && !LoginServerConn->IsConnecting())
    {
        LoginServerConn->Connect();
    }
}
