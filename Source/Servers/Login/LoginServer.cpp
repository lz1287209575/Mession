#include "LoginServer.h"
#include "Common/Runtime/Config.h"
#include "Common/Net/ServerRpcRuntime.h"
#include "Common/Net/NetMessages.h"
#include "Common/Runtime/Json.h"
#include <time.h>

namespace
{
const TMap<MString, const char*> LoginEnvMap = {
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

bool MLoginServer::LoadConfig(const MString& ConfigPath)
{
    TMap<MString, MString> Vars;
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

    
    SServerConnectionConfig RouterConfig(100, EServerType::Router, "Router01", Config.RouterServerAddr, Config.RouterServerPort);
    RouterServerConn = MakeShared<MServerConnection>(RouterConfig);
    RouterServerConn->SetOnAuthenticated([this](auto, const SServerInfo& Info) {
        LOG_INFO("Router server authenticated: %s", Info.ServerName.c_str());
        SendRouterRegister();
    });
    RouterServerConn->SetOnMessage([this](auto, uint8 PacketType, const TByteArray& Data) {
        HandleRouterServerPacket(PacketType, Data);
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
        [this](uint64 Id, const TByteArray& Payload)
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

MString MLoginServer::BuildDebugStatusJson() const
{
    const size_t GatewayCount = GatewayConnections.size();
    const size_t SessionCount = Sessions.size();

    MJsonWriter W = MJsonWriter::Object();
    W.Key("server"); W.Value("Login");
    W.Key("gateways"); W.Value(static_cast<uint64>(GatewayCount));
    W.Key("sessions"); W.Value(static_cast<uint64>(SessionCount));
    const TVector<MString> RpcFunctions = GetGeneratedRpcFunctionNames(EServerType::Login);
    W.Key("rpcManifestCount"); W.Value(static_cast<uint64>(RpcFunctions.size()));
    W.Key("rpcFunctions");
    W.BeginArray();
    for (const MString& Name : RpcFunctions)
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

void MLoginServer::HandleGatewayPacket(uint64 ConnectionId, const TByteArray& Data)
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

    const uint8 PacketType = Data[0];
    const TByteArray Payload(Data.begin() + 1, Data.end());

    if (PacketType == static_cast<uint8>(EServerMessageType::MT_RPC))
    {
        const uint16 HandshakeFunctionId = MGET_STABLE_RPC_FUNCTION_ID("MLoginServer", "Rpc_OnServerHandshake");
        const bool bAuthenticated = PeerIt->second.bAuthenticated;
        if (!bAuthenticated && PeekServerRpcFunctionId(Payload) != HandshakeFunctionId)
        {
            LOG_WARN("Rejecting non-handshake MT_RPC from unauthenticated backend connection %llu",
                     (unsigned long long)ConnectionId);
            return;
        }

        if (!TryInvokeServerRpc(this, ConnectionId, Payload, ERpcType::ServerToServer))
        {
            LOG_WARN("LoginServer MT_RPC packet could not be handled via reflection");
        }
        return;
    }

    LOG_WARN("LoginServer received unexpected non-RPC gateway message type %u from connection %llu",
             static_cast<unsigned>(PacketType),
             static_cast<unsigned long long>(ConnectionId));
}

bool MLoginServer::SendServerPacket(uint64 ConnectionId, uint8 PacketType, const TByteArray& Payload)
{
    auto It = GatewayConnections.find(ConnectionId);
    if (It == GatewayConnections.end() || !It->second.Connection)
    {
        return false;
    }

    TByteArray Packet;
    Packet.reserve(1 + Payload.size());
    Packet.push_back(PacketType);
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

void MLoginServer::HandleRouterServerPacket(uint8 PacketType, const TByteArray& Data)
{
    if (PacketType == static_cast<uint8>(EServerMessageType::MT_RPC))
    {
        if (!TryInvokeServerRpc(this, Data, ERpcType::ServerToServer))
        {
            LOG_WARN("LoginServer router MT_RPC packet could not be handled via reflection");
        }
        return;
    }

    LOG_WARN("Unexpected non-RPC router message type %u", static_cast<unsigned>(PacketType));
}

void MLoginServer::SendRouterRegister()
{
    if (!RouterServerConn || !RouterServerConn->IsConnected())
    {
        return;
    }

    if (!MRpc::CallRemote(
            RouterServerConn,
            "MRouterServer",
            "Rpc_OnPeerServerRegister",
            static_cast<uint32>(2),
            static_cast<uint8>(EServerType::Login),
            MString("Login01"),
            MString("127.0.0.1"),
            Config.ListenPort,
            static_cast<uint16>(0)))
    {
        LOG_WARN("Login->Router register RPC send failed");
    }
}

void MLoginServer::Rpc_OnServerHandshake(uint32 ServerId, uint8 ServerTypeValue, const MString& ServerName)
{
    const uint64 ConnectionId = GetCurrentServerRpcConnectionId();
    auto PeerIt = GatewayConnections.find(ConnectionId);
    if (PeerIt == GatewayConnections.end())
    {
        return;
    }

    SGatewayPeer& Peer = PeerIt->second;
    Peer.ServerId = ServerId;
    Peer.ServerType = static_cast<EServerType>(ServerTypeValue);
    Peer.ServerName = ServerName;
    Peer.bAuthenticated = true;
    LOG_INFO("Gateway %s authenticated (id=%u)",
             Peer.ServerName.c_str(),
             Peer.ServerId);
}

void MLoginServer::Rpc_OnHeartbeat(uint32 Sequence)
{
    LOG_DEBUG("LoginServer heartbeat received (connection=%llu seq=%u)",
              static_cast<unsigned long long>(GetCurrentServerRpcConnectionId()),
              static_cast<unsigned>(Sequence));
}

void MLoginServer::Rpc_OnPlayerLoginRequest(uint64 ClientConnectionId, uint64 PlayerId)
{
    const uint64 GatewayPeerConnectionId = FindAuthenticatedPeerConnectionId(EServerType::Gateway);
    auto GatewayPeerIt = GatewayConnections.find(GatewayPeerConnectionId);
    if (GatewayPeerConnectionId == 0 || GatewayPeerIt == GatewayConnections.end() || !GatewayPeerIt->second.Connection)
    {
        LOG_WARN("No authenticated Gateway peer available for player login response (ClientConnId=%llu, PlayerId=%llu)",
                 (unsigned long long)ClientConnectionId,
                 (unsigned long long)PlayerId);
        return;
    }

    const uint32 SessionKey = CreateSession(PlayerId, ClientConnectionId);
    if (!MRpc::Call(
            GatewayPeerIt->second.Connection,
            EServerType::Gateway,
            "Rpc_OnPlayerLoginResponse",
            ClientConnectionId,
            PlayerId,
            SessionKey))
    {
        LOG_WARN("Login->Gateway player login response RPC send failed: client=%llu player=%llu",
                 static_cast<unsigned long long>(ClientConnectionId),
                 static_cast<unsigned long long>(PlayerId));
        return;
    }

    LOG_INFO("Player %llu logged in, session key: %u",
             (unsigned long long)PlayerId,
             SessionKey);
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

    const bool bSent = MRpc::Call(
        WorldPeerIt->second.Connection,
        EServerType::World,
        "Rpc_OnSessionValidateResponse",
        ValidationRequestId,
        Request.PlayerId,
        bValid);
    if (!bSent)
    {
        LOG_WARN("Login->World session validate response RPC send failed: request=%llu player=%llu",
                 static_cast<unsigned long long>(ValidationRequestId),
                 static_cast<unsigned long long>(Request.PlayerId));
        return;
    }

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

void MLoginServer::OnRouter_ServerRegisterAck(const SNodeRegisterAckMessage& /*Message*/)
{
    LOG_INFO("Login server registered to RouterServer");
}

void MLoginServer::Rpc_OnRouterServerRegisterAck(uint8 Result)
{
    OnRouter_ServerRegisterAck(SNodeRegisterAckMessage{Result});
}
