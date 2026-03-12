#include "GatewayServer.h"
#include "Common/Config.h"
#include "Common/ServerMessages.h"
#include "Messages/NetMessages.h"
#include "Core/Poll.h"

namespace
{
const TMap<FString, const char*> GatewayEnvMap = {
    {"port", "MESSION_GATEWAY_PORT"},
    {"router_addr", "MESSION_ROUTER_ADDR"},
    {"router_port", "MESSION_ROUTER_PORT"},
    {"zone_id", "MESSION_ZONE_ID"},
};

bool IsLoginRoutingMessage(EClientMessageType Type)
{
    return Type == EClientMessageType::MT_Login || Type == EClientMessageType::MT_Handshake;
}
}

bool MGatewayServer::LoadConfig(const FString& ConfigPath)
{
    TMap<FString, FString> Vars;
    if (!ConfigPath.empty())
    {
        MConfig::LoadFromFile(ConfigPath, Vars);
    }
    MConfig::ApplyEnvOverrides(Vars, GatewayEnvMap);
    Config.RouterServerAddr = MConfig::GetStr(Vars, "router_addr", Config.RouterServerAddr);
    Config.RouterServerPort = MConfig::GetU16(Vars, "router_port", Config.RouterServerPort);
    Config.ListenPort = MConfig::GetU16(Vars, "port", Config.ListenPort);
    Config.ZoneId = MConfig::GetU16(Vars, "zone_id", Config.ZoneId);
    return true;
}

bool MGatewayServer::Init(int InPort)
{
    if (InPort > 0)
    {
        Config.ListenPort = static_cast<uint16>(InPort);
    }
    // 创建监听socket
    ListenSocket.Reset(MSocket::CreateListenSocket(Config.ListenPort));

    if (!ListenSocket.IsValid())
    {
        LOG_ERROR("Failed to create listen socket on port %d", Config.ListenPort);
        return false;
    }

    bRunning = true;

    printf("=====================================\n");
    printf("  Mession Gateway Server\n");
    printf("  Listening on port %d (fd=%zd)\n", Config.ListenPort, (intptr_t)ListenSocket.Get());
    printf("=====================================\n");
    
    // 设置本服务器信息
    MServerConnection::SetLocalInfo(1, EServerType::Gateway, "Gateway01");
    
    // 初始化控制面连接
    SServerConnectionConfig RouterConfig(100, EServerType::Router, "Router01", Config.RouterServerAddr, Config.RouterServerPort);
    RouterServerConn = MakeShared<MServerConnection>(RouterConfig);

    RouterServerConn->SetOnConnect([](auto) {
        LOG_INFO("Connected to Router Server!");
    });
    RouterServerConn->SetOnAuthenticated([this](auto, const SServerInfo& Info) {
        LOG_INFO("Router Server authenticated: %s", Info.ServerName.c_str());
        SendRouterRegister();
        QueryRoute(EServerType::Login);
        QueryRoute(EServerType::World);
    });
    RouterServerConn->SetOnMessage([this](auto, uint8 Type, const TArray& Data) {
        HandleRouterServerMessage(Type, Data);
    });

    // 初始化后端长连接
    SServerConnectionConfig LoginConfig(2, EServerType::Login, "Login01", "", 0);
    LoginServerConn = MakeShared<MServerConnection>(LoginConfig);
    
    SServerConnectionConfig WorldConfig(3, EServerType::World, "World01", "", 0);
    WorldServerConn = MakeShared<MServerConnection>(WorldConfig);
    
    // 设置回调
    LoginServerConn->SetOnConnect([](auto) {
        LOG_INFO("Connected to Login Server!");
    });
    LoginServerConn->SetOnAuthenticated([](auto, const SServerInfo& Info) {
        LOG_INFO("Login Server authenticated: %s", Info.ServerName.c_str());
    });
    LoginServerConn->SetOnMessage([this](auto, uint8 Type, const TArray& Data) {
        HandleLoginServerMessage(Type, Data);
    });
    
    WorldServerConn->SetOnConnect([](auto) {
        LOG_INFO("Connected to World Server!");
    });
    WorldServerConn->SetOnAuthenticated([this](auto, const SServerInfo& Info) {
        LOG_INFO("World Server authenticated: %s", Info.ServerName.c_str());
        FlushPendingWorldLogins();
    });
    WorldServerConn->SetOnMessage([this](auto, uint8 Type, const TArray& Data) {
        HandleWorldServerMessage(Type, Data);
    });
    
    // 优先连接 Router，由 Router 返回当前可用后端地址
    RouterServerConn->Connect();
    
    printf("Backend connections initialized\n");
    
    return true;
}

void MGatewayServer::RequestShutdown()
{
    bRunning = false;
    if (ListenSocket.IsValid())
    {
        ListenSocket.Reset();
    }
}

void MGatewayServer::Shutdown()
{
    if (bShutdownDone)
    {
        return;
    }
    bShutdownDone = true;
    bRunning = false;
    
    // 关闭所有客户端连接
    for (auto& [Id, Conn] : ClientConnections)
    {
        if (Conn->Connection)
        {
            Conn->Connection->Close();
        }
    }
    ClientConnections.clear();
    
    // 关闭后端长连接
    if (RouterServerConn)
    {
        RouterServerConn->Disconnect();
    }
    if (LoginServerConn)
    {
        LoginServerConn->Disconnect();
    }
    if (WorldServerConn)
    {
        WorldServerConn->Disconnect();
    }
    
    // 关闭监听socket
    if (ListenSocket.IsValid())
    {
        ListenSocket.Reset();
    }

    LOG_INFO("Gateway server shutdown complete");
}

void MGatewayServer::Tick()
{
    if (!bRunning)
    {
        return;
    }

    static constexpr float BackendTickInterval = 0.016f;
    
    // 接受新客户端
    AcceptClients();

    if (RouterServerConn)
    {
        RouterServerConn->Tick(BackendTickInterval);
    }

    RouteQueryTimer += BackendTickInterval;
    if (RouterServerConn && RouterServerConn->IsConnected() && RouteQueryTimer >= 1.0f)
    {
        RouteQueryTimer = 0.0f;
        if (!LoginServerConn || !LoginServerConn->IsConnected())
        {
            QueryRoute(EServerType::Login);
        }
        if (!WorldServerConn || !WorldServerConn->IsConnected())
        {
            QueryRoute(EServerType::World);
        }
    }
    
    if (LoginServerConn)
    {
        LoginServerConn->Tick(BackendTickInterval);
    }
    if (WorldServerConn)
    {
        WorldServerConn->Tick(BackendTickInterval);
    }

    // 处理客户端消息
    ProcessClientMessages();
}

void MGatewayServer::Run()
{
    if (!bRunning)
    {
        LOG_ERROR("Gateway server not initialized!");
        return;
    }
    
    LOG_INFO("Gateway server running...");
    
    while (bRunning)
    {
        Tick();
        MTime::SleepMilliseconds(16);
    }
}

void MGatewayServer::AcceptClients()
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
        
        auto Client = MakeShared<MClientConnection>(ConnectionId, Connection);
        ClientConnections[ConnectionId] = Client;
        
        LOG_INFO("New client connected: %s (connection_id=%llu)", 
                 Accepted.RemoteAddress.c_str(), (unsigned long long)ConnectionId);
        
        Accepted = MSocket::AcceptConnection(ListenSocket.Get());
    }
}

void MGatewayServer::ProcessClientMessages()
{
    TVector<uint64> DisconnectedClients;
    TVector<SSocketPollItem> PollItems = MSocketPoller::BuildReadableItems(
        ClientConnections,
        [](const TSharedPtr<MClientConnection>& Client) -> INetConnection*
        {
            return Client && Client->Connection ? Client->Connection.get() : nullptr;
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
        auto ClientIt = ClientConnections.find(PollResult.ConnectionId);
        if (ClientIt == ClientConnections.end())
        {
            continue;
        }

        TSharedPtr<MClientConnection>& Client = ClientIt->second;
        if (MSocketPoller::IsReadable(PollResult))
        {
            TArray Packet;
            while (Client->Connection->ReceivePacket(Packet))
            {
                HandleClientPacket(PollResult.ConnectionId, Packet);
            }
            
            if (!Client->Connection->IsConnected())
            {
                DisconnectedClients.push_back(PollResult.ConnectionId);
            }
        }
        else if (MSocketPoller::HasError(PollResult))
        {
            DisconnectedClients.push_back(PollResult.ConnectionId);
        }
    }
    
    // 处理断开连接
    for (uint64 ConnId : DisconnectedClients)
    {
        auto ClientIt = ClientConnections.find(ConnId);
        if (ClientIt != ClientConnections.end())
        {
            const TSharedPtr<MClientConnection>& Client = ClientIt->second;
            if (Client && Client->bAuthenticated && Client->PlayerId != 0)
            {
                if (WorldServerConn && WorldServerConn->IsConnected())
                {
                    SendTypedServerMessage(
                        WorldServerConn,
                        EServerMessageType::MT_PlayerLogout,
                        SPlayerLogoutMessage{Client->PlayerId});
                }

                ResetClientAuthState(Client);
            }
        }

        LOG_INFO("Client disconnected: %llu", (unsigned long long)ConnId);
        ClientConnections.erase(ConnId);
    }
}

TSharedPtr<MClientConnection> MGatewayServer::FindClientByPlayerId(uint64 PlayerId)
{
    if (PlayerId == 0)
    {
        return nullptr;
    }

    for (const auto& [ConnectionId, Client] : ClientConnections)
    {
        (void)ConnectionId;
        if (Client && Client->PlayerId == PlayerId)
        {
            return Client;
        }
    }

    return nullptr;
}

void MGatewayServer::ResetClientAuthState(const TSharedPtr<MClientConnection>& Client)
{
    if (!Client)
    {
        return;
    }

    Client->bAuthenticated = false;
    Client->SessionToken = 0;
    Client->PlayerId = 0;
}

void MGatewayServer::HandleClientPacket(uint64 ConnectionId, const TArray& Data)
{
    if (Data.empty())
    {
        return;
    }
    
    // 简单处理：直接转发到后端
    // 实际应该解析消息头，判断类型
    
    // 假设第一个字节是消息类型
    const EClientMessageType MsgType = (EClientMessageType)Data[0];
    
    // 登录相关消息转发到LoginServer
    if (IsLoginRoutingMessage(MsgType))
    {
        ForwardToBackend(ConnectionId, Data);
    }
    // 游戏消息转发到WorldServer
    else
    {
        ForwardToBackend(ConnectionId, Data);
    }
}

void MGatewayServer::ForwardToBackend(uint64 ConnectionId, const TArray& Data)
{
    auto ClientIt = ClientConnections.find(ConnectionId);
    if (ClientIt == ClientConnections.end())
    {
        return;
    }

    if (Data.empty())
    {
        return;
    }

    const EClientMessageType MsgType = (EClientMessageType)Data[0];
    if (IsLoginRoutingMessage(MsgType))
    {
        if (!LoginServerConn || !LoginServerConn->IsConnected())
        {
            LOG_WARN("Login server unavailable, dropping login request");
            return;
        }

        if (Data.size() < 1 + sizeof(uint64))
        {
            LOG_WARN("Invalid client login packet size: %zu", Data.size());
            return;
        }

        uint64 PlayerId = 0;
        memcpy(&PlayerId, Data.data() + 1, sizeof(PlayerId));

        SendTypedServerMessage(
            LoginServerConn,
            EServerMessageType::MT_PlayerLogin,
            SPlayerLoginRequestMessage{ConnectionId, PlayerId});
        LOG_DEBUG("Forwarded login request for player %llu", (unsigned long long)PlayerId);
        return;
    }

    if (!ClientIt->second->bAuthenticated)
    {
        LOG_WARN("Ignoring unauthenticated client message type %d", (int)MsgType);
        return;
    }

    if (!WorldServerConn || !WorldServerConn->IsConnected())
    {
        LOG_WARN("World server unavailable, dropping client message type %d", (int)MsgType);
        return;
    }

    SendTypedServerMessage(
        WorldServerConn,
        EServerMessageType::MT_PlayerClientSync,
        SPlayerClientSyncMessage{ClientIt->second->PlayerId, Data});
    LOG_DEBUG("Forwarded client message type %d to WorldServer", (int)MsgType);
}

void MGatewayServer::HandleLoginServerMessage(uint8 Type, const TArray& Data)
{
    if (Type != (uint8)EServerMessageType::MT_PlayerLogin)
    {
        return;
    }

    SPlayerLoginResponseMessage Message;
    if (!ParsePayload(Data, Message))
    {
        LOG_WARN("Invalid login server payload size: %zu", Data.size());
        return;
    }

    auto ClientIt = ClientConnections.find(Message.ConnectionId);
    if (ClientIt == ClientConnections.end())
    {
        LOG_WARN("Login response for unknown client %llu", (unsigned long long)Message.ConnectionId);
        return;
    }

    auto& Client = ClientIt->second;
    Client->PlayerId = Message.PlayerId;
    Client->SessionToken = Message.SessionKey;
    Client->bAuthenticated = true;

    TArray Response;
    Response.resize(1 + sizeof(Message.SessionKey) + sizeof(Message.PlayerId));
    Response[0] = (uint8)EClientMessageType::MT_LoginResponse;
    memcpy(Response.data() + 1, &Message.SessionKey, sizeof(Message.SessionKey));
    memcpy(Response.data() + 1 + sizeof(Message.SessionKey), &Message.PlayerId, sizeof(Message.PlayerId));
    Client->Connection->Send(Response.data(), Response.size());

    const uint64 RouteRequestId = QueryRoute(EServerType::World, Message.PlayerId);
    if (RouteRequestId != 0)
    {
        PendingWorldLoginRoutes[RouteRequestId] = {Message.ConnectionId, Message.PlayerId, Message.SessionKey};
    }

    LOG_INFO("Client %llu authenticated as player %llu",
             (unsigned long long)Message.ConnectionId,
             (unsigned long long)Message.PlayerId);
}

void MGatewayServer::HandleWorldServerMessage(uint8 Type, const TArray& Data)
{
    if (Type == (uint8)EServerMessageType::MT_PlayerLogout)
    {
        SPlayerLogoutMessage Message;
        if (!ParsePayload(Data, Message))
        {
            return;
        }

        TSharedPtr<MClientConnection> Client = FindClientByPlayerId(Message.PlayerId);
        if (Client)
        {
            ResetClientAuthState(Client);
        }

        return;
    }

    if (Type == (uint8)EServerMessageType::MT_PlayerClientSync)
    {
        SPlayerClientSyncMessage Message;
        if (!ParsePayload(Data, Message))
        {
            LOG_WARN("Invalid world gameplay payload size: %zu", Data.size());
            return;
        }

        TSharedPtr<MClientConnection> Client = FindClientByPlayerId(Message.PlayerId);
        if (!Client || !Client->Connection)
        {
            LOG_WARN("World sync for unknown player %llu", (unsigned long long)Message.PlayerId);
            return;
        }

        if (!Client->Connection->Send(Message.Data.data(), Message.Data.size()))
        {
            LOG_WARN("Failed to forward world sync to player %llu", (unsigned long long)Message.PlayerId);
        }
    }
}

void MGatewayServer::HandleRouterServerMessage(uint8 Type, const TArray& Data)
{
    switch ((EServerMessageType)Type)
    {
        case EServerMessageType::MT_ServerRegisterAck:
            LOG_INFO("Gateway registered to RouterServer");
            break;

        case EServerMessageType::MT_RouteResponse:
        {
            SRouteResponseMessage Message;
            if (!ParsePayload(Data, Message))
            {
                LOG_WARN("Invalid route response payload size: %zu", Data.size());
                return;
            }

            if (!Message.bFound)
            {
                LOG_WARN("No route available yet for server type %d (request=%llu)",
                         (int)Message.RequestedType,
                         (unsigned long long)Message.RequestId);
                return;
            }

            ApplyRoute(
                Message.ServerInfo.ServerType,
                Message.ServerInfo.ServerId,
                Message.ServerInfo.ServerName,
                Message.ServerInfo.Address,
                Message.ServerInfo.Port);
            if (Message.ServerInfo.ServerType == EServerType::World)
            {
                auto PendingIt = PendingWorldLoginRoutes.find(Message.RequestId);
                if (PendingIt != PendingWorldLoginRoutes.end())
                {
                    FlushPendingWorldLogins();
                }
                else if (Message.PlayerId == 0)
                {
                    FlushPendingWorldLogins();
                }
            }
            break;
        }

        default:
            break;
    }
}

void MGatewayServer::SendRouterRegister()
{
    if (!RouterServerConn || !RouterServerConn->IsConnected())
    {
        return;
    }

    SendTypedServerMessage(
        RouterServerConn,
        EServerMessageType::MT_ServerRegister,
        SServerRegisterMessage{1, EServerType::Gateway, "Gateway01", "127.0.0.1", Config.ListenPort});
}

uint64 MGatewayServer::QueryRoute(EServerType ServerType, uint64 PlayerId)
{
    if (!RouterServerConn || !RouterServerConn->IsConnected())
    {
        return 0;
    }

    const uint64 RequestId = NextRouteRequestId++;
    SendTypedServerMessage(
        RouterServerConn,
        EServerMessageType::MT_RouteQuery,
        SRouteQueryMessage{RequestId, ServerType, PlayerId, Config.ZoneId});
    return RequestId;
}

void MGatewayServer::ApplyRoute(EServerType ServerType, uint32 ServerId, const FString& ServerName, const FString& Address, uint16 Port)
{
    TSharedPtr<MServerConnection> TargetConn;
    if (ServerType == EServerType::Login)
    {
        TargetConn = LoginServerConn;
    }
    else if (ServerType == EServerType::World)
    {
        TargetConn = WorldServerConn;
    }
    else
    {
        return;
    }

    if (!TargetConn)
    {
        return;
    }

    const SServerConnectionConfig& CurrentConfig = TargetConn->GetConfig();
    const bool bRouteChanged =
        CurrentConfig.ServerId != ServerId ||
        CurrentConfig.ServerType != ServerType ||
        CurrentConfig.ServerName != ServerName ||
        CurrentConfig.Address != Address ||
        CurrentConfig.Port != Port;

    if (bRouteChanged && (TargetConn->IsConnected() || TargetConn->IsConnecting()))
    {
        TargetConn->Disconnect();
    }

    SServerConnectionConfig NewConfig(ServerId, ServerType, ServerName, Address, Port);
    NewConfig.HeartbeatInterval = CurrentConfig.HeartbeatInterval;
    NewConfig.ConnectTimeout = CurrentConfig.ConnectTimeout;
    NewConfig.ReconnectInterval = CurrentConfig.ReconnectInterval;
    TargetConn->SetConfig(NewConfig);

    if (!TargetConn->IsConnected() && !TargetConn->IsConnecting())
    {
        TargetConn->Connect();
    }

    if (ServerType == EServerType::World && TargetConn->IsConnected())
    {
        FlushPendingWorldLogins();
    }
}

void MGatewayServer::FlushPendingWorldLogins()
{
    if (!WorldServerConn || !WorldServerConn->IsConnected())
    {
        return;
    }

    TVector<uint64> CompletedRequests;
    for (const auto& [RequestId, Pending] : PendingWorldLoginRoutes)
    {
        SendTypedServerMessage(
            WorldServerConn,
            EServerMessageType::MT_PlayerLogin,
            SPlayerLoginResponseMessage{Pending.ConnectionId, Pending.PlayerId, Pending.SessionKey});
        CompletedRequests.push_back(RequestId);
    }

    for (uint64 RequestId : CompletedRequests)
    {
        PendingWorldLoginRoutes.erase(RequestId);
    }
}
