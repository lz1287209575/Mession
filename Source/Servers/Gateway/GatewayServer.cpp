#include "GatewayServer.h"
#include "Build/Generated/MLoginService.mgenerated.h"
#include "Build/Generated/MWorldService.mgenerated.h"
#include "Common/Config.h"
#include "Common/ServerMessages.h"
#include "Messages/NetMessages.h"
#include "Core/Net/HttpDebugServer.h"
#include "Core/Json.h"

#include <algorithm>

namespace
{
const TMap<FString, const char*> GatewayEnvMap = {
    {"port", "MESSION_GATEWAY_PORT"},
    {"router_addr", "MESSION_ROUTER_ADDR"},
    {"router_port", "MESSION_ROUTER_PORT"},
    {"zone_id", "MESSION_ZONE_ID"},
    {"debug_http_port", "MESSION_GATEWAY_DEBUG_HTTP_PORT"},
};

FString BuildResolvedRouteKey(EServerType ServerType, uint64 PlayerId)
{
    return std::to_string(static_cast<int>(ServerType)) + ":" +
           std::to_string(static_cast<unsigned long long>(PlayerId));
}

const SServerInfo* FindResolvedRouteCache(
    const TMap<FString, SServerInfo>& Cache,
    EServerType ServerType,
    uint64 PlayerId)
{
    const FString PlayerRouteKey = BuildResolvedRouteKey(ServerType, PlayerId);
    auto It = Cache.find(PlayerRouteKey);
    if (It != Cache.end())
    {
        return &It->second;
    }

    if (PlayerId != 0)
    {
        const FString SharedRouteKey = BuildResolvedRouteKey(ServerType, 0);
        It = Cache.find(SharedRouteKey);
        if (It != Cache.end())
        {
            return &It->second;
        }
    }

    return nullptr;
}

using FGeneratedRouteExecutor = EGeneratedClientDispatchResult (MGatewayServer::*)(
    const TSharedPtr<MClientConnection>&,
    const SGeneratedClientRouteRequest&,
    const TArray&);

struct SGeneratedRouteExecutorEntry
{
    const char* RouteName = nullptr;
    const char* WrapMode = nullptr;
    FGeneratedRouteExecutor Execute = nullptr;
};

FString DescribeClientLoginResponsePayload(const SClientLoginResponsePayload& Payload)
{
    return "SClientLoginResponsePayload{SessionKey=" + std::to_string(static_cast<unsigned>(Payload.SessionKey)) +
           ", PlayerId=" + std::to_string(static_cast<unsigned long long>(Payload.PlayerId)) + "}";
}

const char* GetClientPacketLogName(const TArray& Packet)
{
    if (Packet.empty())
    {
        return "Empty";
    }

    if (static_cast<EClientMessageType>(Packet[0]) == EClientMessageType::MT_FunctionCall)
    {
        if (Packet.size() >= 1 + sizeof(uint16))
        {
            uint16 FunctionId = 0;
            std::memcpy(&FunctionId, Packet.data() + 1, sizeof(FunctionId));
            if (const char* FunctionName = GetClientDownlinkFunctionName(FunctionId))
            {
                return FunctionName;
            }
        }

        return "MT_FunctionCall";
    }

    switch (static_cast<EClientMessageType>(Packet[0]))
    {
    case EClientMessageType::MT_Login:
        return "MT_Login";
    case EClientMessageType::MT_Handshake:
        return "MT_Handshake";
    case EClientMessageType::MT_PlayerMove:
        return "MT_PlayerMove";
    case EClientMessageType::MT_RPC:
        return "MT_RPC";
    case EClientMessageType::MT_Chat:
        return "MT_Chat";
    case EClientMessageType::MT_Heartbeat:
        return "MT_Heartbeat";
    case EClientMessageType::MT_Error:
        return "MT_Error";
    case EClientMessageType::MT_FunctionCall:
        return "MT_FunctionCall";
    default:
        return "Unknown";
    }
}

const char* GetGeneratedClientDispatchResultName(EGeneratedClientDispatchResult Result)
{
    switch (Result)
    {
    case EGeneratedClientDispatchResult::NotFound:
        return "NotFound";
    case EGeneratedClientDispatchResult::Routed:
        return "Routed";
    case EGeneratedClientDispatchResult::Handled:
        return "Handled";
    case EGeneratedClientDispatchResult::RouteTargetUnsupported:
        return "RouteTargetUnsupported";
    case EGeneratedClientDispatchResult::MissingFunction:
        return "MissingFunction";
    case EGeneratedClientDispatchResult::MissingBinder:
        return "MissingBinder";
    case EGeneratedClientDispatchResult::ParamBindingFailed:
        return "PayloadDecodeFailed";
    case EGeneratedClientDispatchResult::InvokeFailed:
        return "InvokeFailed";
    case EGeneratedClientDispatchResult::AuthRequired:
        return "AuthRequired";
    case EGeneratedClientDispatchResult::RoutePending:
        return "RoutePending";
    case EGeneratedClientDispatchResult::BackendUnavailable:
        return "BackendUnavailable";
    default:
        return "Unknown";
    }
}

bool SendClientPacketWithLog(const TSharedPtr<MClientConnection>& Client, const TArray& Packet)
{
    if (!Client || !Client->Connection)
    {
        return false;
    }

    LOG_INFO("Outbound client packet: connection_id=%llu player=%llu type=%s bytes=%zu",
             static_cast<unsigned long long>(Client->ConnectionId),
             static_cast<unsigned long long>(Client->PlayerId),
             GetClientPacketLogName(Packet),
             Packet.size());
    return Client->Connection->Send(Packet.data(), static_cast<uint32>(Packet.size()));
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
    MGatewayService::BindHandlers(this);

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
        FlushPendingResolvedClientRoutes(EServerType::World);
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
        if (!DebugServer->Start())
        {
            LOG_ERROR("Gateway debug HTTP failed to start on port %u", static_cast<unsigned>(Config.DebugHttpPort));
            DebugServer.reset();
        }
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
    PendingWorldLoginRoutes.clear();
    PendingResolvedClientRoutes.clear();
    InFlightResolvedRouteRequests.clear();
    ResolvedRouteCache.clear();
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
    uint64 HandshakeCount = 0;
    uint64 LastHandshakePlayerId = 0;
    uint64 HeartbeatCount = 0;
    uint32 LastHeartbeatSequence = 0;
    for (const auto& [ConnectionId, Client] : ClientConnections)
    {
        (void)ConnectionId;
        if (!Client)
        {
            continue;
        }
        HandshakeCount += Client->HandshakeCount;
        if (Client->LastHandshakePlayerId > LastHandshakePlayerId)
        {
            LastHandshakePlayerId = Client->LastHandshakePlayerId;
        }
        HeartbeatCount += Client->HeartbeatCount;
        if (Client->LastHeartbeatSequence > LastHeartbeatSequence)
        {
            LastHeartbeatSequence = Client->LastHeartbeatSequence;
        }
    }

    MJsonWriter W = MJsonWriter::Object();
    W.Key("server"); W.Value("Gateway");
    W.Key("clients"); W.Value(static_cast<uint64>(ClientCount));
    W.Key("backendTotal"); W.Value(static_cast<uint64>(Stats.Total));
    W.Key("backendActive"); W.Value(static_cast<uint64>(Stats.Active));
    W.Key("bytesSent"); W.Value(static_cast<uint64>(Stats.BytesSent));
    W.Key("bytesReceived"); W.Value(static_cast<uint64>(Stats.BytesReceived));
    W.Key("reconnectAttempts"); W.Value(static_cast<uint64>(Stats.ReconnectAttempts));
    W.Key("resolvedRouteCacheSize"); W.Value(static_cast<uint64>(ResolvedRouteCache.size()));
    W.Key("inFlightResolvedRoutes"); W.Value(static_cast<uint64>(InFlightResolvedRouteRequests.size()));
    W.Key("pendingResolvedRouteBuckets"); W.Value(static_cast<uint64>(PendingResolvedClientRoutes.size()));
    W.Key("clientFunctionCallCount"); W.Value(ClientFunctionCallCount);
    W.Key("clientFunctionCallRejectedCount"); W.Value(ClientFunctionCallRejectedCount);
    W.Key("unknownClientFunctionCount"); W.Value(UnknownClientFunctionCount);
    W.Key("clientFunctionDecodeFailureCount"); W.Value(ClientFunctionDecodeFailureCount);
    W.Key("lastClientFunctionId"); W.Value(static_cast<uint64>(LastClientFunctionId));
    W.Key("lastClientFunctionName"); W.Value(LastClientFunctionName);
    W.Key("lastClientFunctionError"); W.Value(LastClientFunctionError);
    W.Key("clientHandshakeCount"); W.Value(HandshakeCount);
    W.Key("lastClientHandshakePlayerId"); W.Value(LastHandshakePlayerId);
    W.Key("clientHeartbeatCount"); W.Value(HeartbeatCount);
    W.Key("lastClientHeartbeatSequence"); W.Value(static_cast<uint64>(LastHeartbeatSequence));
    const TVector<FString> RpcFunctions = GetGeneratedRpcFunctionNames(EServerType::Gateway);
    W.Key("rpcManifestCount"); W.Value(static_cast<uint64>(RpcFunctions.size()));
    W.Key("rpcFunctions");
    W.BeginArray();
    for (const FString& Name : RpcFunctions)
    {
        W.Value(Name);
    }
    W.EndArray();
    W.Key("unsupportedRpc");
    W.BeginArray();
    for (const SGeneratedRpcUnsupportedStat& Stat : GetGeneratedRpcUnsupportedStats(EServerType::Gateway))
    {
        W.BeginObject();
        W.Key("function"); W.Value(Stat.FunctionName);
        W.Key("count"); W.Value(Stat.Count);
        W.EndObject();
    }
    W.EndArray();
    W.EndObject();
    return W.ToString();
}

void MGatewayServer::Client_Handshake(uint64 ClientConnectionId, const SPlayerIdPayload& Request)
{
    auto ClientIt = ClientConnections.find(ClientConnectionId);
    if (ClientIt == ClientConnections.end() || !ClientIt->second)
    {
        LOG_WARN("Client handshake for unknown connection %llu",
                 static_cast<unsigned long long>(ClientConnectionId));
        return;
    }

    TSharedPtr<MClientConnection>& Client = ClientIt->second;
    ++Client->HandshakeCount;
    Client->LastHandshakePlayerId = Request.PlayerId;

    LOG_DEBUG("Client handshake handled locally: connection=%llu player=%llu count=%llu",
              static_cast<unsigned long long>(ClientConnectionId),
              static_cast<unsigned long long>(Request.PlayerId),
              static_cast<unsigned long long>(Client->HandshakeCount));
}

void MGatewayServer::Client_Heartbeat(uint64 ClientConnectionId, const SHeartbeatMessage& Heartbeat)
{
    auto ClientIt = ClientConnections.find(ClientConnectionId);
    if (ClientIt == ClientConnections.end() || !ClientIt->second)
    {
        LOG_WARN("Client heartbeat for unknown connection %llu",
                 static_cast<unsigned long long>(ClientConnectionId));
        return;
    }

    TSharedPtr<MClientConnection>& Client = ClientIt->second;
    ++Client->HeartbeatCount;
    Client->LastHeartbeatSequence = Heartbeat.Sequence;

    LOG_DEBUG("Client heartbeat handled locally: connection=%llu sequence=%u count=%llu",
              static_cast<unsigned long long>(ClientConnectionId),
              static_cast<unsigned>(Heartbeat.Sequence),
              static_cast<unsigned long long>(Client->HeartbeatCount));
}

TSharedPtr<MClientConnection> MGatewayServer::FindClientByPlayerId(uint64 PlayerId)
{
    if (PlayerId == 0)
    {
        return nullptr;
    }

    TSharedPtr<MClientConnection> FallbackClient;
    for (const auto& [ConnectionId, Client] : ClientConnections)
    {
        (void)ConnectionId;
        if (Client && Client->PlayerId == PlayerId)
        {
            if (Client->Connection && Client->Connection->IsConnected())
            {
                return Client;
            }

            if (!FallbackClient)
            {
                FallbackClient = Client;
            }
        }
    }

    return FallbackClient;
}

void MGatewayServer::ResetClientAuthState(const TSharedPtr<MClientConnection>& Client)
{
    if (!Client)
    {
        return;
    }

    if (Client->PlayerId != 0)
    {
        InvalidateResolvedRoutesForPlayer(Client->PlayerId);
        RemovePendingResolvedClientRoutesForPlayer(Client->PlayerId);
    }

    Client->bAuthenticated = false;
    Client->SessionToken = 0;
    Client->PlayerId = 0;
    if (Client->Connection)
    {
        Client->Connection->SetPlayerId(0);
    }
}

bool MGatewayServer::IsGeneratedRouteAuthorized(
    const TSharedPtr<MClientConnection>& Client,
    const SGeneratedClientRouteRequest& Request) const
{
    const FString AuthMode = Request.AuthMode ? FString(Request.AuthMode) : FString();
    if (AuthMode == "Required" && (!Client || !Client->bAuthenticated))
    {
        LOG_WARN("Generated client route requires authenticated client: connection=%llu message=%d",
                 static_cast<unsigned long long>(Request.ConnectionId),
                 static_cast<int>(Request.MessageType));
        return false;
    }

    return true;
}

TArray MGatewayServer::BuildGeneratedRoutePacket(const SGeneratedClientRouteRequest& Request) const
{
    TArray Packet;
    Packet.push_back(static_cast<uint8>(Request.MessageType));
    if (Request.Payload && !Request.Payload->empty())
    {
        Packet.insert(Packet.end(), Request.Payload->begin(), Request.Payload->end());
    }
    return Packet;
}

TSharedPtr<MServerConnection> MGatewayServer::ResolveGeneratedRouteConnection(SGeneratedClientRouteRequest::ERouteKind RouteKind) const
{
    if (RouteKind == SGeneratedClientRouteRequest::ERouteKind::World)
    {
        return WorldServerConn;
    }
    if (RouteKind == SGeneratedClientRouteRequest::ERouteKind::Login)
    {
        return LoginServerConn;
    }
    return nullptr;
}

TSharedPtr<MServerConnection> MGatewayServer::ResolveGeneratedRouteConnection(EServerType TargetServerType) const
{
    if (TargetServerType == EServerType::World)
    {
        return WorldServerConn;
    }
    if (TargetServerType == EServerType::Login)
    {
        return LoginServerConn;
    }
    if (TargetServerType == EServerType::Router)
    {
        return RouterServerConn;
    }
    return nullptr;
}

bool MGatewayServer::EnsureGeneratedRouteResolved(
    const SGeneratedClientRouteRequest& Request,
    const TSharedPtr<MClientConnection>& Client)
{
    if (Request.RouteKind != SGeneratedClientRouteRequest::ERouteKind::RouterResolved)
    {
        return true;
    }

    TSharedPtr<MServerConnection> Connection = ResolveGeneratedRouteConnection(Request.TargetServerType);
    if (Connection && Connection->IsConnected())
    {
        return true;
    }

    if (Request.TargetServerType != EServerType::Unknown)
    {
        const uint64 PlayerId = Client ? Client->PlayerId : 0;
        const FString RouteKey = BuildResolvedRouteKey(Request.TargetServerType, PlayerId);
        if (const SServerInfo* CachedRoute = FindResolvedRouteCache(
                ResolvedRouteCache,
                Request.TargetServerType,
                PlayerId))
        {
            LOG_INFO("Resolved route cache hit: function=%s target=%d player=%llu server=%s",
                     Request.FunctionName ? Request.FunctionName : "",
                     static_cast<int>(Request.TargetServerType),
                     static_cast<unsigned long long>(PlayerId),
                     CachedRoute->ServerName.c_str());
            ApplyRoute(
                CachedRoute->ServerType,
                CachedRoute->ServerId,
                CachedRoute->ServerName,
                CachedRoute->Address,
                CachedRoute->Port);

            Connection = ResolveGeneratedRouteConnection(Request.TargetServerType);
            if (Connection && Connection->IsConnected())
            {
                return true;
            }
        }

        uint64 RouteRequestId = 0;
        auto InFlightIt = InFlightResolvedRouteRequests.find(RouteKey);
        if (InFlightIt != InFlightResolvedRouteRequests.end())
        {
            RouteRequestId = InFlightIt->second;
        }
        else
        {
            RouteRequestId = QueryRoute(Request.TargetServerType, PlayerId);
            if (RouteRequestId != 0)
            {
                InFlightResolvedRouteRequests[RouteKey] = RouteRequestId;
            }
        }

        if (RouteRequestId != 0)
        {
            PendingResolvedClientRoutes[RouteRequestId].push_back(
                SPendingResolvedClientRoute{
                    RouteRequestId,
                    Request.ConnectionId,
                    PlayerId,
                    Request.TargetServerType,
                    Request.WrapMode ? FString(Request.WrapMode) : FString(),
                    BuildGeneratedRoutePacket(Request)});
        }

        LOG_INFO("Generated router-resolved route requested: function=%s target=%d player=%llu request=%llu",
                 Request.FunctionName ? Request.FunctionName : "",
                 static_cast<int>(Request.TargetServerType),
                 static_cast<unsigned long long>(PlayerId),
                 static_cast<unsigned long long>(RouteRequestId));
    }

    return false;
}

EGeneratedClientDispatchResult MGatewayServer::ExecuteGeneratedRouteRawToConnection(
    const TSharedPtr<MServerConnection>& Connection,
    SGeneratedClientRouteRequest::ERouteKind RouteKind,
    const TArray& Packet)
{
    if (!Connection || !Connection->IsConnected())
    {
        const char* RouteLabel = "Route";
        switch (RouteKind)
        {
        case SGeneratedClientRouteRequest::ERouteKind::Login: RouteLabel = "Login"; break;
        case SGeneratedClientRouteRequest::ERouteKind::World: RouteLabel = "World"; break;
        case SGeneratedClientRouteRequest::ERouteKind::RouterResolved: RouteLabel = "RouterResolved"; break;
        default: break;
        }
        LOG_WARN("%s server unavailable, dropping generated routed client message", RouteLabel);
        return EGeneratedClientDispatchResult::BackendUnavailable;
    }

    Connection->SendRaw(Packet);
    return EGeneratedClientDispatchResult::Routed;
}

EGeneratedClientDispatchResult MGatewayServer::ExecuteGeneratedRouteRaw(
    const TSharedPtr<MClientConnection>&,
    const SGeneratedClientRouteRequest& Request,
    const TArray& Packet)
{
    return ExecuteGeneratedRouteRawToConnection(
        ResolveGeneratedRouteConnection(Request.RouteKind),
        Request.RouteKind,
        Packet);
}

EGeneratedClientDispatchResult MGatewayServer::ExecuteGeneratedRoutePlayerClientSync(
    const TSharedPtr<MClientConnection>& Client,
    const SGeneratedClientRouteRequest&,
    const TArray& Packet)
{
    if (!WorldServerConn || !WorldServerConn->IsConnected())
    {
        LOG_WARN("World server unavailable, dropping generated routed client message");
        return EGeneratedClientDispatchResult::BackendUnavailable;
    }

    SendTypedServerMessage(
        WorldServerConn,
        EServerMessageType::MT_PlayerClientSync,
        SPlayerClientSyncMessage{Client ? Client->PlayerId : 0, Packet});
    return EGeneratedClientDispatchResult::Routed;
}

EGeneratedClientDispatchResult MGatewayServer::ExecuteGeneratedRouteLoginRpcOrLegacy(
    const TSharedPtr<MClientConnection>&,
    const SGeneratedClientRouteRequest& Request,
    const TArray&)
{
    if (!LoginServerConn || !LoginServerConn->IsConnected())
    {
        LOG_WARN("Login server unavailable, dropping generated routed client message");
        return EGeneratedClientDispatchResult::BackendUnavailable;
    }

    SPlayerIdPayload RequestPayload;
    auto ParseResult = ParsePayload(*Request.Payload, RequestPayload, "Client_Login");
    if (!ParseResult.IsOk())
    {
        LOG_WARN("ParsePayload failed: %s", ParseResult.GetError().c_str());
        return EGeneratedClientDispatchResult::ParamBindingFailed;
    }

    const bool bRpcSent = MRpc::TryRpcOrTypedLegacy(
        [&]()
        {
            return MRpc::MLoginService::Rpc_OnPlayerLoginRequest(
                LoginServerConn,
                Request.ConnectionId,
                RequestPayload.PlayerId);
        },
        LoginServerConn,
        EServerMessageType::MT_PlayerLogin,
        SPlayerLoginRequestMessage{Request.ConnectionId, RequestPayload.PlayerId});
    if (!bRpcSent)
    {
        LOG_INFO("Generated login route forwarded via legacy path: ConnectionId=%llu, PlayerId=%llu",
                 static_cast<unsigned long long>(Request.ConnectionId),
                 static_cast<unsigned long long>(RequestPayload.PlayerId));
        return EGeneratedClientDispatchResult::Routed;
    }

    LOG_INFO("Generated login route forwarded via generated RPC: ConnectionId=%llu, PlayerId=%llu",
             static_cast<unsigned long long>(Request.ConnectionId),
             static_cast<unsigned long long>(RequestPayload.PlayerId));
    return EGeneratedClientDispatchResult::Routed;
}

EGeneratedClientDispatchResult MGatewayServer::ExecuteGeneratedRouteByPolicy(
    const TSharedPtr<MClientConnection>& Client,
    const FString& RouteKey,
    const FString& WrapMode,
    const SGeneratedClientRouteRequest* Request,
    const TArray& Packet)
{
    static const SGeneratedRouteExecutorEntry GeneratedRouteExecutors[] = {
        {"World", "PlayerClientSync", &MGatewayServer::ExecuteGeneratedRoutePlayerClientSync},
        {"World", "Raw", &MGatewayServer::ExecuteGeneratedRouteRaw},
        {"Login", "Raw", &MGatewayServer::ExecuteGeneratedRouteRaw},
        {"Login", "LoginRpcOrLegacy", &MGatewayServer::ExecuteGeneratedRouteLoginRpcOrLegacy},
    };

    for (const SGeneratedRouteExecutorEntry& Entry : GeneratedRouteExecutors)
    {
        if (RouteKey == (Entry.RouteName ? Entry.RouteName : "") &&
            WrapMode == (Entry.WrapMode ? Entry.WrapMode : ""))
        {
            if (!Request)
            {
                return EGeneratedClientDispatchResult::InvokeFailed;
            }
            return (this->*Entry.Execute)(Client, *Request, Packet);
        }
    }

    LOG_WARN("Unsupported generated client route: route=%s wrap=%s function=%s",
             RouteKey.c_str(),
             WrapMode.c_str(),
             (Request && Request->FunctionName) ? Request->FunctionName : "");
    return EGeneratedClientDispatchResult::RouteTargetUnsupported;
}

void MGatewayServer::InvalidateResolvedRoute(EServerType ServerType, uint64 PlayerId)
{
    const FString RouteKey = BuildResolvedRouteKey(ServerType, PlayerId);
    if (ResolvedRouteCache.erase(RouteKey) > 0)
    {
        LOG_INFO("Resolved route cache invalidated: target=%d player=%llu",
                 static_cast<int>(ServerType),
                 static_cast<unsigned long long>(PlayerId));
    }
}

void MGatewayServer::InvalidateResolvedRoutesForPlayer(uint64 PlayerId)
{
    if (PlayerId == 0)
    {
        return;
    }

    InvalidateResolvedRoute(EServerType::World, PlayerId);
    InvalidateResolvedRoute(EServerType::Login, PlayerId);
    InFlightResolvedRouteRequests.erase(BuildResolvedRouteKey(EServerType::World, PlayerId));
    InFlightResolvedRouteRequests.erase(BuildResolvedRouteKey(EServerType::Login, PlayerId));
}

void MGatewayServer::RemovePendingResolvedClientRoutesForPlayer(uint64 PlayerId)
{
    if (PlayerId == 0)
    {
        return;
    }

    TVector<uint64> EmptyRequests;
    for (auto& [RequestId, PendingList] : PendingResolvedClientRoutes)
    {
        PendingList.erase(
            std::remove_if(
                PendingList.begin(),
                PendingList.end(),
                [PlayerId](const SPendingResolvedClientRoute& Pending)
                {
                    return Pending.PlayerId == PlayerId;
                }),
            PendingList.end());

        if (PendingList.empty())
        {
            EmptyRequests.push_back(RequestId);
        }
    }

    for (uint64 RequestId : EmptyRequests)
    {
        PendingResolvedClientRoutes.erase(RequestId);
    }
}

EGeneratedClientDispatchResult MGatewayServer::HandleGeneratedClientRoute(const SGeneratedClientRouteRequest& Request)
{
    auto ClientIt = ClientConnections.find(Request.ConnectionId);
    if (ClientIt == ClientConnections.end() || !ClientIt->second)
    {
        LOG_WARN("Generated client route for unknown connection %llu",
                 static_cast<unsigned long long>(Request.ConnectionId));
        return EGeneratedClientDispatchResult::InvokeFailed;
    }

    const TSharedPtr<MClientConnection>& Client = ClientIt->second;
    if (!IsGeneratedRouteAuthorized(Client, Request))
    {
        return EGeneratedClientDispatchResult::AuthRequired;
    }

    if (!EnsureGeneratedRouteResolved(Request, Client))
    {
        return EGeneratedClientDispatchResult::RoutePending;
    }

    const TArray Packet = BuildGeneratedRoutePacket(Request);
    const FString RouteKey =
        (Request.RouteKind == SGeneratedClientRouteRequest::ERouteKind::RouterResolved &&
         Request.TargetServerType == EServerType::World)
            ? FString("World")
            : FString(Request.RouteName ? Request.RouteName : "");
    const FString WrapMode = Request.WrapMode ? FString(Request.WrapMode) : FString();
    return ExecuteGeneratedRouteByPolicy(Client, RouteKey, WrapMode, &Request, Packet);
}

bool MGatewayServer::HandleClientFunctionCall(uint64 ConnectionId, const TArray& Data)
{
    LastClientFunctionError.clear();

    if (Data.size() < 1 + sizeof(uint16) + sizeof(uint32))
    {
        ++ClientFunctionDecodeFailureCount;
        ++ClientFunctionCallRejectedCount;
        LastClientFunctionError = "PacketTooSmall";
        LOG_WARN("Unified client function packet too small: connection=%llu size=%zu",
                 static_cast<unsigned long long>(ConnectionId),
                 Data.size());
        return true;
    }

    size_t Offset = 1;
    uint16 FunctionId = 0;
    uint32 PayloadSize = 0;
    std::memcpy(&FunctionId, Data.data() + Offset, sizeof(FunctionId));
    Offset += sizeof(FunctionId);
    std::memcpy(&PayloadSize, Data.data() + Offset, sizeof(PayloadSize));
    Offset += sizeof(PayloadSize);

    LastClientFunctionId = FunctionId;
    if (Offset + PayloadSize > Data.size())
    {
        ++ClientFunctionDecodeFailureCount;
        ++ClientFunctionCallRejectedCount;
        LastClientFunctionError = "PayloadOutOfRange";
        LOG_WARN("Unified client function payload out of range: connection=%llu function_id=%u size=%zu payload=%u",
                 static_cast<unsigned long long>(ConnectionId),
                 static_cast<unsigned>(FunctionId),
                 Data.size(),
                 static_cast<unsigned>(PayloadSize));
        return true;
    }

    TArray Payload;
    if (PayloadSize > 0)
    {
        Payload.resize(PayloadSize);
        std::memcpy(Payload.data(), Data.data() + Offset, PayloadSize);
    }

    const SGeneratedClientDispatchOutcome Outcome =
        DispatchGeneratedClientFunction(this, ConnectionId, FunctionId, Payload);
    if (Outcome.FunctionName)
    {
        LastClientFunctionName = Outcome.FunctionName;
    }
    else
    {
        LastClientFunctionName.clear();
    }

    if (Outcome.Result == EGeneratedClientDispatchResult::NotFound)
    {
        ++UnknownClientFunctionCount;
        ++ClientFunctionCallRejectedCount;
        LastClientFunctionError = "UnknownFunctionId";
        LOG_WARN("Unknown unified client function: connection=%llu function_id=%u",
                 static_cast<unsigned long long>(ConnectionId),
                 static_cast<unsigned>(FunctionId));
        return true;
    }

    ++ClientFunctionCallCount;
    if (Outcome.Result != EGeneratedClientDispatchResult::Handled &&
        Outcome.Result != EGeneratedClientDispatchResult::Routed)
    {
        ++ClientFunctionCallRejectedCount;
        LastClientFunctionError = GetGeneratedClientDispatchResultName(Outcome.Result);
    }

    return true;
}

void MGatewayServer::HandleClientPacket(uint64 ConnectionId, const TArray& Data)
{
    if (Data.empty())
    {
        return;
    }

    const EClientMessageType MsgType = (EClientMessageType)Data[0];
    if (MsgType == EClientMessageType::MT_FunctionCall)
    {
        HandleClientFunctionCall(ConnectionId, Data);
        return;
    }

    LOG_WARN("Rejected non-function client message type %d: MT_FunctionCall is the only supported ingress", (int)MsgType);
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
    if (Client->Connection)
    {
        Client->Connection->SetPlayerId(Message.PlayerId);
    }

    const SClientLoginResponsePayload ResponsePayload{Message.SessionKey, Message.PlayerId};
    LOG_INFO("Preparing client login response: ConnectionId=%llu, Response=%s",
             static_cast<unsigned long long>(Message.ConnectionId),
             DescribeClientLoginResponsePayload(ResponsePayload).c_str());

    TArray Packet;
    if (!BuildClientFunctionCallPacketForPayload(MClientDownlink::Id_OnLoginResponse(), ResponsePayload, Packet))
    {
        LOG_WARN("Failed to build unified client login response packet");
        return;
    }
    SendClientPacketWithLog(Client, Packet);

    const uint64 RouteRequestId = QueryRoute(EServerType::World, Message.PlayerId);
    if (RouteRequestId != 0)
    {
        PendingWorldLoginRoutes[RouteRequestId] = {Message.ConnectionId, Message.PlayerId, Message.SessionKey};
    }

    LOG_INFO("Client %llu authenticated as player %llu",
             (unsigned long long)Message.ConnectionId,
             (unsigned long long)Message.PlayerId);
}

void MGatewayServer::Rpc_OnPlayerLoginResponse(uint64 ClientConnectionId, uint64 PlayerId, uint32 SessionKey)
{
    OnLogin_PlayerLogin(SPlayerLoginResponseMessage{ClientConnectionId, PlayerId, SessionKey});
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

    LOG_INFO("Outbound client packet: connection_id=%llu player=%llu type=%s bytes=%zu",
             static_cast<unsigned long long>(Client->ConnectionId),
             static_cast<unsigned long long>(Message.PlayerId),
             GetClientPacketLogName(Message.Data),
             Message.Data.size());
    if (!Client->Connection->Send(Message.Data.data(), Message.Data.size()))
    {
        LOG_WARN("Failed to forward world sync to player %llu (connection_id=%llu, connected=%s, bytes=%zu)",
                 (unsigned long long)Message.PlayerId,
                 (unsigned long long)Client->ConnectionId,
                 Client->Connection->IsConnected() ? "true" : "false",
                 Message.Data.size());
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
    InFlightResolvedRouteRequests.erase(BuildResolvedRouteKey(Message.RequestedType, Message.PlayerId));

    if (!Message.bFound)
    {
        InvalidateResolvedRoute(Message.RequestedType, Message.PlayerId);
        LOG_WARN("No route available yet for server type %d (request=%llu)",
                 (int)Message.RequestedType,
                 (unsigned long long)Message.RequestId);
        return;
    }

    ResolvedRouteCache[BuildResolvedRouteKey(Message.RequestedType, Message.PlayerId)] = Message.ServerInfo;
    LOG_INFO("Resolved route cache updated: target=%d player=%llu server=%s(%u)",
             static_cast<int>(Message.RequestedType),
             static_cast<unsigned long long>(Message.PlayerId),
             Message.ServerInfo.ServerName.c_str(),
             Message.ServerInfo.ServerId);

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

        auto PendingResolvedIt = PendingResolvedClientRoutes.find(Message.RequestId);
        if (PendingResolvedIt != PendingResolvedClientRoutes.end() || Message.PlayerId == 0)
        {
            FlushPendingResolvedClientRoutes(EServerType::World);
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
        FlushPendingResolvedClientRoutes(EServerType::World);
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
        MRpc::TryRpcOrTypedLegacy(
            [&]()
            {
                return MRpc::MWorldService::Rpc_OnPlayerLoginRequest(
                    WorldServerConn,
                    Pending.ConnectionId,
                    Pending.PlayerId,
                    Pending.SessionKey);
            },
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

void MGatewayServer::FlushPendingResolvedClientRoutes(EServerType ServerType)
{
    TSharedPtr<MServerConnection> TargetConnection = ResolveGeneratedRouteConnection(ServerType);
    if (!TargetConnection || !TargetConnection->IsConnected())
    {
        return;
    }

    TVector<uint64> CompletedRequests;
    for (const auto& [RequestId, PendingList] : PendingResolvedClientRoutes)
    {
        bool bAllFlushed = true;
        for (const SPendingResolvedClientRoute& Pending : PendingList)
        {
            if (Pending.TargetServerType != ServerType)
            {
                bAllFlushed = false;
                continue;
            }

            auto ClientIt = ClientConnections.find(Pending.ConnectionId);
            if (ClientIt == ClientConnections.end() || !ClientIt->second)
            {
                LOG_WARN("Pending generated client route dropped for missing connection %llu",
                         static_cast<unsigned long long>(Pending.ConnectionId));
                continue;
            }

            SGeneratedClientRouteRequest ReplayRequest;
            ReplayRequest.ConnectionId = Pending.ConnectionId;
            ReplayRequest.RouteKind = ServerType == EServerType::World
                ? SGeneratedClientRouteRequest::ERouteKind::World
                : SGeneratedClientRouteRequest::ERouteKind::None;
            ReplayRequest.RouteName = GetServerTypeDisplayName(ServerType);
            ReplayRequest.TargetServerType = ServerType;
            ReplayRequest.WrapMode = Pending.WrapMode.c_str();

            if (ExecuteGeneratedRouteByPolicy(
                    ClientIt->second,
                    GetServerTypeDisplayName(ServerType),
                    Pending.WrapMode,
                    &ReplayRequest,
                    Pending.Packet) != EGeneratedClientDispatchResult::Routed)
            {
                bAllFlushed = false;
            }
        }

        if (bAllFlushed)
        {
            CompletedRequests.push_back(RequestId);
        }
    }

    for (uint64 RequestId : CompletedRequests)
    {
        PendingResolvedClientRoutes.erase(RequestId);
    }
}
