#include "GatewayServer.h"
#include "Common/Config.h"
#include "Common/ServerMessages.h"
#include "Messages/NetMessages.h"
#include "Core/Net/HttpDebugServer.h"
#include "Core/Json.h"

namespace
{
const TMap<FString, const char*> GatewayEnvMap = {
    {"port", "MESSION_GATEWAY_PORT"},
    {"router_addr", "MESSION_ROUTER_ADDR"},
    {"router_port", "MESSION_ROUTER_PORT"},
    {"zone_id", "MESSION_ZONE_ID"},
    {"debug_http_port", "MESSION_GATEWAY_DEBUG_HTTP_PORT"},
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
    Config.DebugHttpPort = MConfig::GetU16(Vars, "debug_http_port", Config.DebugHttpPort);
    return true;
}

bool MGatewayServer::Init(int InPort)
{
    if (InPort > 0)
    {
        Config.ListenPort = static_cast<uint16>(InPort);
    }
    bRunning = true;

    MLogger::LogStartupBanner("GatewayServer", Config.ListenPort, 0);
    
    // 设置本服务器信息
    MServerConnection::SetLocalInfo(1, EServerType::Gateway, "Gateway01");
    
    // 初始化消息分发器
    InitLoginMessageHandlers();
    InitWorldMessageHandlers();
    InitRouterMessageHandlers();
    MGatewayService::SetHandler_Rpc_OnPlayerLoginResponse([this](uint64 ConnectionId, uint64 PlayerId, uint32 SessionKey)
    {
        OnLogin_PlayerLogin(SPlayerLoginResponseMessage{ConnectionId, PlayerId, SessionKey});
    });

    // 初始化控制面连接（通过统一连接管理器）
    SServerConnectionConfig RouterConfig(100, EServerType::Router, "Router01", Config.RouterServerAddr, Config.RouterServerPort);
    RouterServerConn = BackendConnectionManager.AddServer(RouterConfig);

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

    // 初始化后端长连接（通过统一连接管理器）
    SServerConnectionConfig LoginConfig(2, EServerType::Login, "Login01", "", 0);
    LoginServerConn = BackendConnectionManager.AddServer(LoginConfig);
    
    SServerConnectionConfig WorldConfig(3, EServerType::World, "World01", "", 0);
    WorldServerConn = BackendConnectionManager.AddServer(WorldConfig);
    
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
    
    LOG_INFO("Backend connections initialized");

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

uint16 MGatewayServer::GetListenPort() const
{
    return Config.ListenPort;
}

void MGatewayServer::OnAccept(uint64 ConnId, TSharedPtr<INetConnection> Conn)
{
    Conn->SetNonBlocking(true);
    auto Client = MakeShared<MClientConnection>(ConnId, Conn);
    ClientConnections[ConnId] = Client;
    LOG_INFO("New client connected (connection_id=%llu)", (unsigned long long)ConnId);
    EventLoop.RegisterConnection(ConnId, Conn,
        [this](uint64 Id, const TArray& Payload)
        {
            HandleClientPacket(Id, Payload);
        },
        [this](uint64 Id)
        {
            auto It = ClientConnections.find(Id);
            if (It != ClientConnections.end())
            {
                TSharedPtr<MClientConnection>& Client = It->second;
                if (Client && Client->bAuthenticated && Client->PlayerId != 0 &&
                    WorldServerConn && WorldServerConn->IsConnected())
                {
                    SendTypedServerMessage(
                        WorldServerConn,
                        EServerMessageType::MT_PlayerLogout,
                        SPlayerLogoutMessage{Client->PlayerId});
                }
                if (Client)
                {
                    ResetClientAuthState(Client);
                }
                LOG_INFO("Client disconnected: %llu", (unsigned long long)Id);
                ClientConnections.erase(Id);
            }
        });
}

void MGatewayServer::ShutdownConnections()
{
    for (auto& [Id, Conn] : ClientConnections)
    {
        if (Conn->Connection)
        {
            Conn->Connection->Close();
        }
    }
    ClientConnections.clear();
    BackendConnectionManager.DisconnectAll();
    if (DebugServer)
    {
        DebugServer->Stop();
        DebugServer.reset();
    }
    RouterServerConn.reset();
    LoginServerConn.reset();
    WorldServerConn.reset();
    LOG_INFO("Gateway server shutdown complete");
}

void MGatewayServer::OnRunStarted()
{
    LOG_INFO("Gateway server running...");
}

void MGatewayServer::Tick()
{
    if (!bRunning)
    {
        return;
    }
    TickBackends();
}

void MGatewayServer::TickBackends()
{
    static constexpr float BackendTickInterval = 0.016f;

    // 统一驱动所有后端连接
    BackendConnectionManager.Tick(BackendTickInterval);

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
}

FString MGatewayServer::BuildDebugStatusJson() const
{
    const size_t ClientCount = ClientConnections.size();
    const SConnectionManagerStats Stats = BackendConnectionManager.GetStats();

    MJsonWriter W = MJsonWriter::Object();
    W.Key("server"); W.Value("Gateway");
    W.Key("clients"); W.Value(static_cast<uint64>(ClientCount));
    W.Key("backendTotal"); W.Value(static_cast<uint64>(Stats.Total));
    W.Key("backendActive"); W.Value(static_cast<uint64>(Stats.Active));
    W.Key("bytesSent"); W.Value(static_cast<uint64>(Stats.BytesSent));
    W.Key("bytesReceived"); W.Value(static_cast<uint64>(Stats.BytesReceived));
    W.Key("reconnectAttempts"); W.Value(static_cast<uint64>(Stats.ReconnectAttempts));
    return W.ToString();
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

        TArray Payload(Data.begin() + 1, Data.end());
        SPlayerIdPayload IdPayload;
        auto ParseResult = ParsePayload(Payload, IdPayload, "client_login");
        if (!ParseResult.IsOk())
        {
            LOG_WARN("ParsePayload failed: %s", ParseResult.GetError().c_str());
            return;
        }

        const uint16 FunctionId = MLoginService::GetFunctionId_Rpc_OnPlayerLoginRequest();
        if (FunctionId == 0)
        {
            LOG_WARN("MLoginService::GetFunctionId_Rpc_OnPlayerLoginRequest returned 0, fallback to legacy MT_PlayerLogin");

            SendTypedServerMessage(
                LoginServerConn,
                EServerMessageType::MT_PlayerLogin,
                SPlayerLoginRequestMessage{ConnectionId, IdPayload.PlayerId});
            LOG_DEBUG("Forwarded legacy login request for player %llu", (unsigned long long)IdPayload.PlayerId);
            return;
        }

        TArray RpcData;
        BuildServerRpcPayload(
            FunctionId,
            BuildRpcPayloadForCall<&MLoginService::Rpc_OnPlayerLoginRequest>(
                ConnectionId,
                IdPayload.PlayerId),
            RpcData);

        const uint8* RpcPayload = RpcData.empty() ? nullptr : RpcData.data();
        LoginServerConn->Send(
            static_cast<uint8>(EServerMessageType::MT_RPC),
            RpcPayload,
            static_cast<uint32>(RpcData.size()));

        LOG_DEBUG("Forwarded login request for player %llu", (unsigned long long)IdPayload.PlayerId);
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
    if (Type == static_cast<uint8>(EServerMessageType::MT_RPC))
    {
        if (!TryInvokeServerRpc(&GatewayService, Data, ERpcType::ServerToServer))
        {
            LOG_WARN("GatewayServer MT_RPC packet could not be handled via reflection");
        }
        return;
    }

    LoginMessageDispatcher.Dispatch(Type, Data);
}

void MGatewayServer::HandleWorldServerMessage(uint8 Type, const TArray& Data)
{
    WorldMessageDispatcher.Dispatch(Type, Data);
}

void MGatewayServer::HandleRouterServerMessage(uint8 Type, const TArray& Data)
{
    if (Type == static_cast<uint8>(EServerMessageType::MT_RPC))
    {
        if (!TryInvokeServerRpc(this, Data, ERpcType::ServerToServer) &&
            !TryInvokeServerRpc(&GatewayService, Data, ERpcType::ServerToServer))
        {
            LOG_WARN("GatewayServer router MT_RPC packet could not be handled via reflection");
        }
        return;
    }

    RouterMessageDispatcher.Dispatch(Type, Data);
}

void MGatewayServer::InitLoginMessageHandlers()
{
    MREGISTER_SERVER_MESSAGE_HANDLER(
        LoginMessageDispatcher,
        EServerMessageType::MT_PlayerLogin,
        &MGatewayServer::OnLogin_PlayerLogin,
        "MT_PlayerLogin");
}

void MGatewayServer::InitWorldMessageHandlers()
{
    MREGISTER_SERVER_MESSAGE_HANDLER(
        WorldMessageDispatcher,
        EServerMessageType::MT_PlayerLogout,
        &MGatewayServer::OnWorld_PlayerLogout,
        "MT_PlayerLogout");

    MREGISTER_SERVER_MESSAGE_HANDLER(
        WorldMessageDispatcher,
        EServerMessageType::MT_PlayerClientSync,
        &MGatewayServer::OnWorld_PlayerClientSync,
        "MT_PlayerClientSync");
}

void MGatewayServer::InitRouterMessageHandlers()
{
    MREGISTER_SERVER_MESSAGE_HANDLER(
        RouterMessageDispatcher,
        EServerMessageType::MT_ServerRegisterAck,
        &MGatewayServer::OnRouter_ServerRegisterAck,
        "MT_ServerRegisterAck");

    MREGISTER_SERVER_MESSAGE_HANDLER(
        RouterMessageDispatcher,
        EServerMessageType::MT_RouteResponse,
        &MGatewayServer::OnRouter_RouteResponse,
        "MT_RouteResponse");
}

void MGatewayServer::OnLogin_PlayerLogin(const SPlayerLoginResponseMessage& Message)
{
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

    TArray RespPayload = BuildPayload(SClientLoginResponsePayload{Message.SessionKey, Message.PlayerId});
    TArray Packet;
    Packet.reserve(1 + RespPayload.size());
    Packet.push_back(static_cast<uint8>(EClientMessageType::MT_LoginResponse));
    Packet.insert(Packet.end(), RespPayload.begin(), RespPayload.end());
    Client->Connection->Send(Packet.data(), static_cast<uint32>(Packet.size()));

    const uint64 RouteRequestId = QueryRoute(EServerType::World, Message.PlayerId);
    if (RouteRequestId != 0)
    {
        PendingWorldLoginRoutes[RouteRequestId] = {Message.ConnectionId, Message.PlayerId, Message.SessionKey};
    }

    LOG_INFO("Client %llu authenticated as player %llu",
             (unsigned long long)Message.ConnectionId,
             (unsigned long long)Message.PlayerId);
}

void MGatewayServer::OnWorld_PlayerLogout(const SPlayerLogoutMessage& Message)
{
    TSharedPtr<MClientConnection> Client = FindClientByPlayerId(Message.PlayerId);
    if (Client)
    {
        ResetClientAuthState(Client);
    }
}

void MGatewayServer::OnWorld_PlayerClientSync(const SPlayerClientSyncMessage& Message)
{
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

void MGatewayServer::OnRouter_ServerRegisterAck(const SServerRegisterAckMessage& /*Message*/)
{
    LOG_INFO("Gateway registered to RouterServer");
}

void MGatewayServer::Rpc_OnRouterServerRegisterAck(uint8 Result)
{
    OnRouter_ServerRegisterAck(SServerRegisterAckMessage{Result});
}

void MGatewayServer::OnRouter_RouteResponse(const SRouteResponseMessage& Message)
{
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
}

void MGatewayServer::Rpc_OnRouterRouteResponse(
    uint64 RequestId,
    uint8 RequestedTypeValue,
    uint64 PlayerId,
    bool bFound,
    uint32 ServerId,
    uint8 ServerTypeValue,
    const FString& ServerName,
    const FString& Address,
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
    const uint16 FunctionId = MWorldService::GetFunctionId_Rpc_OnPlayerLoginRequest();
    for (const auto& [RequestId, Pending] : PendingWorldLoginRoutes)
    {
        if (FunctionId == 0)
        {
            SendTypedServerMessage(
                WorldServerConn,
                EServerMessageType::MT_PlayerLogin,
                SPlayerLoginResponseMessage{Pending.ConnectionId, Pending.PlayerId, Pending.SessionKey});
        }
        else
        {
            TArray RpcData;
            BuildServerRpcPayload(
                FunctionId,
                BuildRpcPayloadForCall<&MWorldService::Rpc_OnPlayerLoginRequest>(
                    Pending.ConnectionId,
                    Pending.PlayerId,
                    Pending.SessionKey),
                RpcData);

            const uint8* RpcPayload = RpcData.empty() ? nullptr : RpcData.data();
            WorldServerConn->Send(
                static_cast<uint8>(EServerMessageType::MT_RPC),
                RpcPayload,
                static_cast<uint32>(RpcData.size()));
        }

        CompletedRequests.push_back(RequestId);
    }

    for (uint64 RequestId : CompletedRequests)
    {
        PendingWorldLoginRoutes.erase(RequestId);
    }
}
