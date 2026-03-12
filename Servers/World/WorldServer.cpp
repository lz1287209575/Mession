#include "WorldServer.h"
#include "Common/Config.h"
#include "Common/StringUtils.h"
#include "Messages/NetMessages.h"
#include "Core/Poll.h"

namespace
{
const TMap<FString, const char*> WorldEnvMap = {
    {"port", "MESSION_WORLD_PORT"},
    {"router_addr", "MESSION_ROUTER_ADDR"},
    {"router_port", "MESSION_ROUTER_PORT"},
    {"login_addr", "MESSION_LOGIN_ADDR"},
    {"login_port", "MESSION_LOGIN_PORT"},
    {"zone_id", "MESSION_ZONE_ID"},
};

class MGatewayClientTunnelConnection : public INetConnection
{
public:
    MGatewayClientTunnelConnection(
        TFunction<bool(const void*, uint32)> InSendCallback,
        TFunction<bool()> InIsConnectedCallback)
        : SendCallback(InSendCallback)
        , IsConnectedCallback(InIsConnectedCallback)
    {
    }

    bool Send(const void* Data, uint32 Size) override
    {
        if (!Data || Size == 0 || !SendCallback)
        {
            return false;
        }

        return SendCallback(Data, Size);
    }

    bool Receive(void* /*Buffer*/, uint32 /*BufferSize*/, uint32& OutBytesReceived) override
    {
        OutBytesReceived = 0;
        return false;
    }

    uint64 GetPlayerId() const override
    {
        return PlayerId;
    }

    void SetPlayerId(uint64 Id) override
    {
        PlayerId = Id;
    }

    bool ReceivePacket(TArray& /*OutPayload*/) override
    {
        return false;
    }

    void Close() override
    {
    }

    bool IsConnected() const override
    {
        return IsConnectedCallback ? IsConnectedCallback() : false;
    }

    TSocketFd GetSocketFd() const override
    {
        return INVALID_SOCKET_FD;
    }

    void SetNonBlocking(bool /*bNonBlocking*/) override
    {
    }

private:
    uint64 PlayerId = 0;
    TFunction<bool(const void*, uint32)> SendCallback;
    TFunction<bool()> IsConnectedCallback;
};
}

MWorldServer::MWorldServer()
{
    ReplicationDriver = new MReplicationDriver();
}

bool MWorldServer::LoadConfig(const FString& ConfigPath)
{
    TMap<FString, FString> Vars;
    if (!ConfigPath.empty())
    {
        MConfig::LoadFromFile(ConfigPath, Vars);
    }
    MConfig::ApplyEnvOverrides(Vars, WorldEnvMap);
    Config.ListenPort = MConfig::GetU16(Vars, "port", Config.ListenPort);
    Config.RouterServerAddr = MConfig::GetStr(Vars, "router_addr", Config.RouterServerAddr);
    Config.RouterServerPort = MConfig::GetU16(Vars, "router_port", Config.RouterServerPort);
    Config.LoginServerAddr = MConfig::GetStr(Vars, "login_addr", Config.LoginServerAddr);
    Config.LoginServerPort = MConfig::GetU16(Vars, "login_port", Config.LoginServerPort);
    Config.ZoneId = MConfig::GetU16(Vars, "zone_id", Config.ZoneId);
    Config.MaxPlayers = MConfig::GetU32(Vars, "max_players", Config.MaxPlayers);
    Config.ServerName = MConfig::GetStr(Vars, "server_name", Config.ServerName);
    return true;
}

bool MWorldServer::Init(int InPort)
{
    if (InPort > 0)
    {
        Config.ListenPort = static_cast<uint16>(InPort);
    }
    MServerConnection::SetLocalInfo(3, EServerType::World, Config.ServerName);

    // 创建监听socket
    ListenSocket.Reset(MSocket::CreateListenSocket(Config.ListenPort));
    if (!ListenSocket.IsValid())
    {
        LOG_ERROR("Failed to create listen socket on port %d", InPort);
        return false;
    }

    bRunning = true;

    MLogger::LogStartupBanner("WorldServer", Config.ListenPort, static_cast<intptr_t>(ListenSocket.Get()));

    SServerConnectionConfig RouterConfig(100, EServerType::Router, "Router01", Config.RouterServerAddr, Config.RouterServerPort);
    RouterServerConn = MakeShared<MServerConnection>(RouterConfig);
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
    LoginServerConn = MakeShared<MServerConnection>(LoginConfig);
    LoginServerConn->SetOnAuthenticated([](auto, const SServerInfo& Info) {
        LOG_INFO("Login server authenticated: %s", Info.ServerName.c_str());
    });
    LoginServerConn->SetOnMessage([this](auto, uint8 Type, const TArray& Data) {
        HandleLoginServerMessage(Type, Data);
    });
    
    return true;
}

void MWorldServer::RequestShutdown()
{
    bRunning = false;
    if (ListenSocket.IsValid())
    {
        ListenSocket.Reset();
    }
}

void MWorldServer::Shutdown()
{
    if (bShutdownDone)
    {
        return;
    }
    bShutdownDone = true;
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
    
    // 清理复制系统
    delete ReplicationDriver;
    ReplicationDriver = nullptr;
    
    // 关闭监听socket
    if (ListenSocket.IsValid())
    {
        ListenSocket.Reset();
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

    LoadReportTimer += DEFAULT_TICK_RATE;
    if (RouterServerConn && RouterServerConn->IsConnected() && LoadReportTimer >= 5.0f)
    {
        LoadReportTimer = 0.0f;
        SendLoadReport();
    }

    if (LoginServerConn)
    {
        LoginServerConn->Tick(DEFAULT_TICK_RATE);
    }
    
    // 处理消息
    ProcessMessages();

    // 刷新后端连接发送缓冲，确保复制包等能发出（避免 EAGAIN 后滞留）
    for (auto& [Id, Peer] : BackendConnections)
    {
        if (Peer.Connection)
        {
            Peer.Connection->FlushSendBuffer();
        }
    }
    
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
        MTime::SleepMilliseconds(16);
    }
}

void MWorldServer::AcceptConnections()
{
    SAcceptedSocket Accepted = MSocket::AcceptConnection(ListenSocket.Get());

    while (Accepted.IsValid())
    {
        uint64 ConnectionId = NextConnectionId++;
        TSharedPtr<INetConnection> Connection = MakeShared<MTcpConnection>(
            std::move(Accepted.Socket),
            Accepted.RemoteAddress,
            Accepted.RemotePort);
        Connection->SetNonBlocking(true);

        SBackendPeer Peer;
        Peer.Connection = Connection;
        BackendConnections[ConnectionId] = Peer;
        
        LOG_INFO("New connection: %s (connection_id=%llu)", 
                 Accepted.RemoteAddress.c_str(), (unsigned long long)ConnectionId);
        
        Accepted = MSocket::AcceptConnection(ListenSocket.Get());
    }
}

void MWorldServer::ProcessMessages()
{
    TVector<uint64> DisconnectedConnections;
    TVector<SSocketPollItem> PollItems = MSocketPoller::BuildReadableItems(
        BackendConnections,
        [](SBackendPeer& Peer) -> INetConnection*
        {
            return Peer.Connection ? Peer.Connection.get() : nullptr;
        });

    if (PollItems.empty())
    {
        return;
    }

    TVector<SSocketPollResult> PollResults;
    int32 Ret = MSocketPoller::PollReadable(PollItems, PollResults, 10);
    
    if (Ret < 0)
    {
        return;
    }

    for (const SSocketPollResult& PollResult : PollResults)
    {
        auto PeerIt = BackendConnections.find(PollResult.ConnectionId);
        if (PeerIt == BackendConnections.end())
        {
            continue;
        }

        SBackendPeer& Peer = PeerIt->second;
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
    
    // 处理断开的连接
    for (uint64 ConnId : DisconnectedConnections)
    {
        TVector<uint64> PlayerIdsToRemove;
        for (const auto& [PlayerId, Player] : Players)
        {
            if (Player.GatewayConnectionId == ConnId)
            {
                PlayerIdsToRemove.push_back(PlayerId);
            }
        }

        for (uint64 PlayerId : PlayerIdsToRemove)
        {
            RemovePlayer(PlayerId);
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
            auto ParseResult = ParsePayload(Payload, Message, "MT_PlayerLogin");
            if (!ParseResult.IsOk())
            {
                LOG_WARN("ParsePayload failed: %s", ParseResult.GetError().c_str());
                return;
            }

            RequestSessionValidation(ConnectionId, Message.PlayerId, Message.SessionKey);
            break;
        }

        case EServerMessageType::MT_PlayerLogout:
        {
            if (!Peer.bAuthenticated || Peer.ServerType != EServerType::Gateway)
            {
                return;
            }

            SPlayerLogoutMessage Message;
            auto ParseResult = ParsePayload(Payload, Message, "MT_PlayerLogout");
            if (!ParseResult.IsOk())
            {
                LOG_WARN("ParsePayload failed: %s", ParseResult.GetError().c_str());
                return;
            }

            RemovePlayer(Message.PlayerId);
            break;
        }

        case EServerMessageType::MT_PlayerClientSync:
        {
            if (!Peer.bAuthenticated || Peer.ServerType != EServerType::Gateway)
            {
                return;
            }

            SPlayerClientSyncMessage Message;
            auto ParseResult = ParsePayload(Payload, Message, "MT_PlayerClientSync");
            if (!ParseResult.IsOk())
            {
                LOG_WARN("ParsePayload failed: %s", ParseResult.GetError().c_str());
                return;
            }

            HandleGameplayPacket(Message.PlayerId, Message.Data);
            break;
        }

        default:
            break;
    }
}

void MWorldServer::HandleGameplayPacket(uint64 PlayerId, const TArray& Data)
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
            TArray Payload(Data.begin() + 1, Data.end());
            SPlayerMovePayload MovePayload;
            auto ParseResult = ParsePayload(Payload, MovePayload, "MT_PlayerMove");
            if (!ParseResult.IsOk())
            {
                LOG_WARN("ParsePayload failed: %s", ParseResult.GetError().c_str());
                return;
            }

            auto* Player = GetPlayerById(PlayerId);
            if (!Player || !Player->Character)
            {
                return;
            }

            const SVector NewPos(MovePayload.X, MovePayload.Y, MovePayload.Z);
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

void MWorldServer::AddPlayer(uint64 PlayerId, const FString& Name, uint64 GatewayConnectionId)
{
    if (Players.find(PlayerId) != Players.end())
    {
        return;
    }

    SPlayer Player;
    Player.PlayerId = PlayerId;
    Player.Name = Name;
    Player.GatewayConnectionId = GatewayConnectionId;
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

    TSharedPtr<INetConnection> ReplicationConnection = MakeShared<MGatewayClientTunnelConnection>(
        [this, GatewayConnectionId, PlayerId](const void* Data, uint32 Size) -> bool
        {
            TArray PacketBytes;
            const uint8* ByteData = static_cast<const uint8*>(Data);
            PacketBytes.insert(PacketBytes.end(), ByteData, ByteData + Size);
            const bool bOk = SendServerMessage(
                GatewayConnectionId,
                EServerMessageType::MT_PlayerClientSync,
                SPlayerClientSyncMessage{PlayerId, PacketBytes});
            if (!bOk)
            {
                LOG_WARN("Tunnel send to gateway conn %llu for player %llu failed",
                         (unsigned long long)GatewayConnectionId, (unsigned long long)PlayerId);
            }
            return bOk;
        },
        [this, GatewayConnectionId]() -> bool
        {
            auto GatewayIt = BackendConnections.find(GatewayConnectionId);
            return GatewayIt != BackendConnections.end() &&
                GatewayIt->second.bAuthenticated &&
                GatewayIt->second.ServerType == EServerType::Gateway &&
                GatewayIt->second.Connection &&
                GatewayIt->second.Connection->IsConnected();
        });
    ReplicationConnection->SetPlayerId(PlayerId);

    // 注册到复制系统
    ReplicationDriver->RegisterActor(Character);
    ReplicationDriver->AddConnection(PlayerId, ReplicationConnection);
    ReplicationDriver->AddRelevantActor(PlayerId, Character->GetObjectId());
    ReplicationDriver->BroadcastActorCreate(Character, PlayerId);

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
    ReplicationDriver->RemoveConnection(Player.PlayerId);
    if (Player.Character)
    {
        ReplicationDriver->BroadcastActorDestroy(Player.Character->GetObjectId());
        delete Player.Character;
    }

    BroadcastToScenes(
        (uint8)EServerMessageType::MT_PlayerLogout,
        BuildPayload(SPlayerSceneLeaveMessage{Player.PlayerId, static_cast<uint16>(Player.CurrentSceneId)}));
    
    Players.erase(It);
    
    LOG_INFO("Player %llu removed from world", (unsigned long long)PlayerId);
}

SPlayer* MWorldServer::GetPlayerById(uint64 PlayerId)
{
    auto It = Players.find(PlayerId);
    return (It != Players.end()) ? &It->second : nullptr;
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
    auto ParseResult = ParsePayload(Data, Message, "MT_SessionValidateResponse");
    if (!ParseResult.IsOk())
    {
        LOG_WARN("ParsePayload failed: %s", ParseResult.GetError().c_str());
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

    AddPlayer(Message.PlayerId, "Player" + MString::ToString(Message.PlayerId), Pending.GatewayConnectionId);
    auto* Player = GetPlayerById(Message.PlayerId);
    if (Player)
    {
        Player->SessionKey = Pending.SessionKey;
    }
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

    SendTypedServerMessage(
        LoginServerConn,
        EServerMessageType::MT_SessionValidateRequest,
        SSessionValidateRequestMessage{ValidationRequestId, PlayerId, SessionKey});
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
            auto ParseResult = ParsePayload(Data, Message, "MT_RouteResponse");
            if (!ParseResult.IsOk())
            {
                LOG_WARN("ParsePayload failed: %s", ParseResult.GetError().c_str());
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
        SServerRegisterMessage{3, EServerType::World, Config.ServerName, "127.0.0.1", Config.ListenPort, Config.ZoneId});
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
        SRouteQueryMessage{NextRouteRequestId++, EServerType::Login, 0, 0});
}

void MWorldServer::SendLoadReport()
{
    if (!RouterServerConn || !RouterServerConn->IsConnected())
    {
        return;
    }

    uint32 OnlineCount = 0;
    for (const auto& [PlayerId, Player] : Players)
    {
        (void)PlayerId;
        if (Player.bOnline)
        {
            ++OnlineCount;
        }
    }

    SendTypedServerMessage(
        RouterServerConn,
        EServerMessageType::MT_ServerLoadReport,
        SServerLoadReportMessage{OnlineCount, Config.MaxPlayers});
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
