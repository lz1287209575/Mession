#include "LoginServer.h"
#include "Build/Generated/MGatewayService.mgenerated.h"
#include "Build/Generated/MWorldService.mgenerated.h"
#include "Common/Config.h"
#include "Common/ServerRpcRuntime.h"
#include "Messages/NetMessages.h"
#include "Core/Json.h"
#include <time.h>

namespace
{
const TMap<FString, const char*> LoginEnvMap = {
    {"port", "MESSION_LOGIN_PORT"},
    {"router_addr", "MESSION_ROUTER_ADDR"},
    {"router_port", "MESSION_ROUTER_PORT"},
    {"debug_http_port", "MESSION_LOGIN_DEBUG_HTTP_PORT"},
};
}

MLoginServer::MLoginServer()
{
    std::random_device Rd;
    Rng = std::mt19937(Rd());
}

bool MLoginServer::LoadConfig(const FString& ConfigPath)
{
    TMap<FString, FString> Vars;
    if (!ConfigPath.empty())
    {
        MConfig::LoadFromFile(ConfigPath, Vars);
    }
    MConfig::ApplyEnvOverrides(Vars, LoginEnvMap);
    Config.ListenPort = MConfig::GetU16(Vars, "port", Config.ListenPort);
    Config.RouterServerAddr = MConfig::GetStr(Vars, "router_addr", Config.RouterServerAddr);
    Config.RouterServerPort = MConfig::GetU16(Vars, "router_port", Config.RouterServerPort);
    Config.SessionKeyMin = MConfig::GetU32(Vars, "session_key_min", Config.SessionKeyMin);
    Config.SessionKeyMax = MConfig::GetU32(Vars, "session_key_max", Config.SessionKeyMax);
    Config.DebugHttpPort = MConfig::GetU16(Vars, "debug_http_port", Config.DebugHttpPort);
    return true;
}

bool MLoginServer::Init(int InPort)
{
    if (InPort > 0)
    {
        Config.ListenPort = static_cast<uint16>(InPort);
    }
    MServerConnection::SetLocalInfo(2, EServerType::Login, "Login01");

    bRunning = true;

    MLogger::LogStartupBanner("LoginServer", Config.ListenPort, 0);

    // 初始化服务器消息分发器
    InitGatewayMessageHandlers();
    InitRouterMessageHandlers();

    MLoginService::BindHandlers(this);

    SServerConnectionConfig RouterConfig(100, EServerType::Router, "Router01", Config.RouterServerAddr, Config.RouterServerPort);
    RouterServerConn = MakeShared<MServerConnection>(RouterConfig);
    RouterServerConn->SetOnAuthenticated([this](auto, const SServerInfo& Info) {
        LOG_INFO("Router server authenticated: %s", Info.ServerName.c_str());
        SendRouterRegister();
    });
    RouterServerConn->SetOnMessage([this](auto, uint8 Type, const TArray& Data) {
        HandleRouterServerMessage(Type, Data);
    });
    RouterServerConn->Connect();

    // 启动调试 HTTP 服务器（仅当配置端口 > 0 时）
    if (Config.DebugHttpPort > 0)
    {
        DebugServer = TUniquePtr<MHttpDebugServer>(new MHttpDebugServer(
            Config.DebugHttpPort,
            [this]() { return BuildDebugStatusJson(); }));
        if (!DebugServer->Start())
        {
            LOG_ERROR("Login debug HTTP failed to start on port %u", static_cast<unsigned>(Config.DebugHttpPort));
            DebugServer.reset();
        }
    }

    return true;
}

uint16 MLoginServer::GetListenPort() const
{
    return Config.ListenPort;
}

void MLoginServer::OnAccept(uint64 ConnId, TSharedPtr<INetConnection> Conn)
{
    Conn->SetNonBlocking(true);
    SGatewayPeer Peer;
    Peer.Connection = Conn;
    GatewayConnections[ConnId] = Peer;
    LOG_INFO("New gateway connected (connection_id=%llu)", (unsigned long long)ConnId);
    EventLoop.RegisterConnection(ConnId, Conn,
        [this](uint64 Id, const TArray& Payload)
        {
            HandleGatewayPacket(Id, Payload);
        },
        [this](uint64 Id)
        {
            LOG_INFO("Gateway disconnected: %llu", (unsigned long long)Id);
            GatewayConnections.erase(Id);
        });
}

void MLoginServer::ShutdownConnections()
{
    for (auto& [Id, Peer] : GatewayConnections)
    {
        if (Peer.Connection)
        {
            Peer.Connection->Close();
        }
    }
    GatewayConnections.clear();
    if (RouterServerConn)
    {
        RouterServerConn->Disconnect();
    }
    Sessions.clear();
    PlayerSessions.clear();
    if (DebugServer)
    {
        DebugServer->Stop();
        DebugServer.reset();
    }
    LOG_INFO("Login server shutdown complete");
}

void MLoginServer::Tick()
{
    if (!bRunning)
    {
        return;
    }
    TickBackends();
}

void MLoginServer::TickBackends()
{
    if (RouterServerConn)
    {
        RouterServerConn->Tick(0.016f);
    }

    const uint64 Now = static_cast<uint64>(time(nullptr));
    TVector<uint32> ExpiredSessions;
    for (auto& [Key, Session] : Sessions)
    {
        if (Session.ExpireTime < Now)
        {
            ExpiredSessions.push_back(Key);
        }
    }
    for (uint32 Key : ExpiredSessions)
    {
        RemoveSession(Key);
    }
}

void MLoginServer::OnRunStarted()
{
    LOG_INFO("Login server running...");
}

FString MLoginServer::BuildDebugStatusJson() const
{
    const size_t GatewayCount = GatewayConnections.size();
    const size_t SessionCount = Sessions.size();

    MJsonWriter W = MJsonWriter::Object();
    W.Key("server"); W.Value("Login");
    W.Key("gateways"); W.Value(static_cast<uint64>(GatewayCount));
    W.Key("sessions"); W.Value(static_cast<uint64>(SessionCount));
    const TVector<FString> RpcFunctions = GetGeneratedRpcFunctionNames(EServerType::Login);
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
    for (const SGeneratedRpcUnsupportedStat& Stat : GetGeneratedRpcUnsupportedStats(EServerType::Login))
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

void MLoginServer::HandleGatewayPacket(uint64 ConnectionId, const TArray& Data)
{
    if (Data.empty())
    {
        return;
    }

    auto PeerIt = GatewayConnections.find(ConnectionId);
    if (PeerIt == GatewayConnections.end())
    {
        return;
    }

    const uint8 MsgType = Data[0];
    const TArray Payload(Data.begin() + 1, Data.end());

    if (MsgType == static_cast<uint8>(EServerMessageType::MT_RPC))
    {
        if (!PeerIt->second.bAuthenticated)
        {
            LOG_WARN("Rejecting MT_RPC from unauthenticated backend connection %llu",
                     (unsigned long long)ConnectionId);
            return;
        }

        if (!TryInvokeServerRpc(&LoginService, Payload, ERpcType::ServerToServer))
        {
            LOG_WARN("LoginServer MT_RPC packet could not be handled via reflection");
        }
        return;
    }

    GatewayMessageDispatcher.Dispatch(ConnectionId, MsgType, Payload);
}

bool MLoginServer::SendServerMessage(uint64 ConnectionId, uint8 Type, const TArray& Payload)
{
    auto It = GatewayConnections.find(ConnectionId);
    if (It == GatewayConnections.end() || !It->second.Connection)
    {
        return false;
    }

    TArray Packet;
    Packet.reserve(1 + Payload.size());
    Packet.push_back(Type);
    Packet.insert(Packet.end(), Payload.begin(), Payload.end());
    return It->second.Connection->Send(Packet.data(), Packet.size());
}

uint32 MLoginServer::CreateSession(uint64 PlayerId, uint64 ConnectionId)
{
    uint32 SessionKey = GenerateSessionKey();
    
    SSession Session;
    Session.PlayerId = PlayerId;
    Session.SessionKey = SessionKey;
    Session.ConnectionId = ConnectionId;
    Session.ExpireTime = time(nullptr) + 3600; // 1小时过期
    
    Sessions[SessionKey] = Session;
    PlayerSessions[PlayerId] = SessionKey;
    
    return SessionKey;
}

bool MLoginServer::ValidateSession(uint32 SessionKey, uint64& OutPlayerId)
{
    auto It = Sessions.find(SessionKey);
    if (It == Sessions.end())
    {
        return false;
    }
    
    // 检查是否过期
    if (It->second.ExpireTime < static_cast<uint64>(time(nullptr)))
    {
        RemoveSession(SessionKey);
        return false;
    }
    
    OutPlayerId = It->second.PlayerId;
    return true;
}

void MLoginServer::RemoveSession(uint32 SessionKey)
{
    auto It = Sessions.find(SessionKey);
    if (It != Sessions.end())
    {
        PlayerSessions.erase(It->second.PlayerId);
        Sessions.erase(It);
        LOG_DEBUG("Session %u removed", SessionKey);
    }
}

uint32 MLoginServer::GenerateSessionKey()
{
    std::uniform_int_distribution<uint32> Dist(Config.SessionKeyMin, Config.SessionKeyMax);
    return Dist(Rng);
}

uint64 MLoginServer::FindAuthenticatedPeerConnectionId(EServerType ServerType) const
{
    for (const auto& [PeerConnectionId, Peer] : GatewayConnections)
    {
        if (Peer.bAuthenticated && Peer.ServerType == ServerType && Peer.Connection)
        {
            return PeerConnectionId;
        }
    }

    return 0;
}

void MLoginServer::HandleRouterServerMessage(uint8 Type, const TArray& Data)
{
    if (Type == static_cast<uint8>(EServerMessageType::MT_RPC))
    {
        if (!TryInvokeServerRpc(this, Data, ERpcType::ServerToServer) &&
            !TryInvokeServerRpc(&LoginService, Data, ERpcType::ServerToServer))
        {
            LOG_WARN("LoginServer router MT_RPC packet could not be handled via reflection");
        }
        return;
    }

    RouterMessageDispatcher.Dispatch(Type, Data);
}

void MLoginServer::InitGatewayMessageHandlers()
{
    MREGISTER_SERVER_MESSAGE_HANDLER(
        GatewayMessageDispatcher,
        EServerMessageType::MT_ServerHandshake,
        &MLoginServer::OnGateway_ServerHandshake,
        "MT_ServerHandshake");

    MREGISTER_SERVER_MESSAGE_HANDLER(
        GatewayMessageDispatcher,
        EServerMessageType::MT_Heartbeat,
        &MLoginServer::OnGateway_Heartbeat,
        "MT_Heartbeat");

    MREGISTER_SERVER_MESSAGE_HANDLER(
        GatewayMessageDispatcher,
        EServerMessageType::MT_PlayerLogin,
        &MLoginServer::OnGateway_PlayerLogin,
        "MT_PlayerLogin");

    MREGISTER_SERVER_MESSAGE_HANDLER(
        GatewayMessageDispatcher,
        EServerMessageType::MT_SessionValidateRequest,
        &MLoginServer::OnGateway_SessionValidateRequest,
        "MT_SessionValidateRequest");
}

void MLoginServer::SendRouterRegister()
{
    if (!RouterServerConn || !RouterServerConn->IsConnected())
    {
        return;
    }

    SendTypedServerMessage(
        RouterServerConn,
        EServerMessageType::MT_ServerRegister,
        SServerRegisterMessage{2, EServerType::Login, "Login01", "127.0.0.1", Config.ListenPort});
}

void MLoginServer::InitRouterMessageHandlers()
{
    MREGISTER_SERVER_MESSAGE_HANDLER(
        RouterMessageDispatcher,
        EServerMessageType::MT_ServerRegisterAck,
        &MLoginServer::OnRouter_ServerRegisterAck,
        "MT_ServerRegisterAck");
}

void MLoginServer::OnGateway_ServerHandshake(uint64 ConnectionId, const TArray& Payload)
{
    auto PeerIt = GatewayConnections.find(ConnectionId);
    if (PeerIt == GatewayConnections.end())
    {
        return;
    }

    SGatewayPeer& Peer = PeerIt->second;
    if (!Peer.bAuthenticated)
    {
        SPlayerIdPayload IdPayload;
        auto ParseResult = ParsePayload(Payload, IdPayload, "handshake_minimal");
        if (ParseResult.IsOk() && IdPayload.PlayerId != 0)
        {
            const uint32 SessionKey = CreateSession(IdPayload.PlayerId, ConnectionId);
            TArray Packet;
            if (!BuildClientFunctionCallPacketForPayload(
                    MClientDownlink::Id_OnLoginResponse(),
                    SClientLoginResponsePayload{SessionKey, IdPayload.PlayerId},
                    Packet))
            {
                LOG_WARN("Failed to build unified login response packet for player %llu",
                         (unsigned long long)IdPayload.PlayerId);
                return;
            }
            Peer.Connection->Send(Packet.data(), static_cast<uint32>(Packet.size()));

            LOG_INFO("Player %llu logged in, session key: %u",
                     (unsigned long long)IdPayload.PlayerId,
                     SessionKey);
            return;
        }
    }

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
    LOG_INFO("Gateway %s authenticated (id=%u)",
             Peer.ServerName.c_str(),
             Peer.ServerId);
}

void MLoginServer::OnGateway_Heartbeat(uint64 ConnectionId, const SHeartbeatMessage& /*Message*/)
{
    SendServerMessage(ConnectionId, EServerMessageType::MT_HeartbeatAck, SEmptyServerMessage{});
}

void MLoginServer::OnGateway_PlayerLogin(uint64 ClientConnectionId, const SPlayerLoginRequestMessage& Request)
{
    // Service RPC reaches this handler only after arriving on an authenticated backend connection.
    // Here ClientConnectionId is the gateway-side client connection id carried as business data,
    // not the LoginServer socket id of the backend peer.
    const uint64 GatewayPeerConnectionId = FindAuthenticatedPeerConnectionId(EServerType::Gateway);
    auto GatewayPeerIt = GatewayConnections.find(GatewayPeerConnectionId);
    if (GatewayPeerConnectionId == 0 || GatewayPeerIt == GatewayConnections.end() || !GatewayPeerIt->second.Connection)
    {
        LOG_WARN("No authenticated Gateway peer available for player login response (ClientConnId=%llu, PlayerId=%llu)",
                 (unsigned long long)Request.ConnectionId,
                 (unsigned long long)Request.PlayerId);
        return;
    }

    const uint32 SessionKey = CreateSession(Request.PlayerId, ClientConnectionId);
    MRpc::TryRpcOrTypedLegacy(
        [&]()
        {
            return MRpc::MGatewayService::Rpc_OnPlayerLoginResponse(
                GatewayPeerIt->second.Connection,
                ClientConnectionId,
                Request.PlayerId,
                SessionKey);
        },
        GatewayPeerIt->second.Connection,
        EServerMessageType::MT_PlayerLogin,
        SPlayerLoginResponseMessage{ClientConnectionId, Request.PlayerId, SessionKey});

    LOG_INFO("Player %llu logged in, session key: %u",
             (unsigned long long)Request.PlayerId, SessionKey);
}

void MLoginServer::Rpc_OnPlayerLoginRequest(uint64 ClientConnectionId, uint64 PlayerId)
{
    OnGateway_PlayerLogin(
        ClientConnectionId,
        SPlayerLoginRequestMessage{ClientConnectionId, PlayerId});
}

void MLoginServer::OnGateway_SessionValidateRequest(uint64 ValidationRequestId, const SSessionValidateRequestMessage& Request)
{
    // For the reflected service path, ValidationRequestId is the world-side request correlation id.
    // Authentication has already been enforced by the backend connection that delivered MT_RPC.
    const uint64 WorldPeerConnectionId = FindAuthenticatedPeerConnectionId(EServerType::World);
    auto WorldPeerIt = GatewayConnections.find(WorldPeerConnectionId);
    if (WorldPeerConnectionId == 0 || WorldPeerIt == GatewayConnections.end() || !WorldPeerIt->second.Connection)
    {
        LOG_WARN("No authenticated World peer available for session validation response (RequestId=%llu, PlayerId=%llu)",
                 (unsigned long long)Request.ConnectionId,
                 (unsigned long long)Request.PlayerId);
        return;
    }

    uint64 ValidatedPlayerId = 0;
    const bool bValid = ValidateSession(Request.SessionKey, ValidatedPlayerId) && ValidatedPlayerId == Request.PlayerId;

    MRpc::TryRpcOrTypedLegacy(
        [&]()
        {
            return MRpc::MWorldService::Rpc_OnSessionValidateResponse(
                WorldPeerIt->second.Connection,
                ValidationRequestId,
                Request.PlayerId,
                bValid);
        },
        WorldPeerIt->second.Connection,
        EServerMessageType::MT_SessionValidateResponse,
        SSessionValidateResponseMessage{ValidationRequestId, Request.PlayerId, bValid});

    LOG_INFO("Session validation for player %llu on connection %llu: %s",
             (unsigned long long)Request.PlayerId,
             (unsigned long long)ValidationRequestId,
             bValid ? "valid" : "invalid");
}

void MLoginServer::Rpc_OnSessionValidateRequest(uint64 ValidationRequestId, uint64 PlayerId, uint32 SessionKey)
{
    OnGateway_SessionValidateRequest(
        ValidationRequestId,
        SSessionValidateRequestMessage{ValidationRequestId, PlayerId, SessionKey});
}

void MLoginServer::OnRouter_ServerRegisterAck(const SServerRegisterAckMessage& /*Message*/)
{
    LOG_INFO("Login server registered to RouterServer");
}

void MLoginServer::Rpc_OnRouterServerRegisterAck(uint8 Result)
{
    OnRouter_ServerRegisterAck(SServerRegisterAckMessage{Result});
}
