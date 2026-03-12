#include "LoginServer.h"
#include "Common/Config.h"
#include "Messages/NetMessages.h"
#include "Core/Poll.h"
#include <time.h>

namespace
{
const TMap<FString, const char*> LoginEnvMap = {
    {"port", "MESSION_LOGIN_PORT"},
    {"router_addr", "MESSION_ROUTER_ADDR"},
    {"router_port", "MESSION_ROUTER_PORT"},
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
    return true;
}

bool MLoginServer::Init(int InPort)
{
    if (InPort > 0)
    {
        Config.ListenPort = static_cast<uint16>(InPort);
    }
    MServerConnection::SetLocalInfo(2, EServerType::Login, "Login01");

    // 创建监听socket
    ListenSocket.Reset(MSocket::CreateListenSocket(Config.ListenPort));
    if (!ListenSocket.IsValid())
    {
        LOG_ERROR("Failed to create listen socket on port %d", Config.ListenPort);
        return false;
    }

    bRunning = true;

    MLogger::LogStartupBanner("LoginServer", Config.ListenPort, static_cast<intptr_t>(ListenSocket.Get()));

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
    
    return true;
}

void MLoginServer::RequestShutdown()
{
    bRunning = false;
    if (ListenSocket.IsValid())
    {
        ListenSocket.Reset();
    }
}

void MLoginServer::Shutdown()
{
    if (bShutdownDone)
    {
        return;
    }
    bShutdownDone = true;
    bRunning = false;
    
    // 关闭所有网关连接
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
    
    // 清理会话
    Sessions.clear();
    PlayerSessions.clear();
    
    // 关闭监听socket
    if (ListenSocket.IsValid())
    {
        ListenSocket.Reset();
    }
    
    LOG_INFO("Login server shutdown complete");
}

void MLoginServer::Tick()
{
    if (!bRunning)
    {
        return;
    }
    
    // 接受新网关连接
    AcceptGateways();
    
    // 处理网关消息
    ProcessGatewayMessages();

    if (RouterServerConn)
    {
        RouterServerConn->Tick(0.016f);
    }
    
    // 清理过期会话
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

void MLoginServer::Run()
{
    if (!bRunning)
    {
        LOG_ERROR("Login server not initialized!");
        return;
    }
    
    LOG_INFO("Login server running...");
    
    while (bRunning)
    {
        Tick();
        MTime::SleepMilliseconds(16);
    }
}

void MLoginServer::AcceptGateways()
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

        SGatewayPeer Peer;
        Peer.Connection = Connection;
        GatewayConnections[ConnectionId] = Peer;
        
        LOG_INFO("New gateway connected: %s (connection_id=%llu)", 
                 Accepted.RemoteAddress.c_str(), (unsigned long long)ConnectionId);
        
        Accepted = MSocket::AcceptConnection(ListenSocket.Get());
    }
}

void MLoginServer::ProcessGatewayMessages()
{
    TVector<uint64> DisconnectedGateways;
    TVector<SSocketPollItem> PollItems = MSocketPoller::BuildReadableItems(
        GatewayConnections,
        [](SGatewayPeer& Peer) -> INetConnection*
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
        auto PeerIt = GatewayConnections.find(PollResult.ConnectionId);
        if (PeerIt == GatewayConnections.end())
        {
            continue;
        }

        SGatewayPeer& Peer = PeerIt->second;
        if (MSocketPoller::IsReadable(PollResult))
        {
            TArray Packet;
            while (Peer.Connection->ReceivePacket(Packet))
            {
                HandleGatewayPacket(PollResult.ConnectionId, Packet);
            }
            
            if (!Peer.Connection->IsConnected())
            {
                DisconnectedGateways.push_back(PollResult.ConnectionId);
            }
        }
        else if (MSocketPoller::HasError(PollResult))
        {
            DisconnectedGateways.push_back(PollResult.ConnectionId);
        }
    }
    
    for (uint64 ConnId : DisconnectedGateways)
    {
        LOG_INFO("Gateway disconnected: %llu", (unsigned long long)ConnId);
        GatewayConnections.erase(ConnId);
    }
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

    SGatewayPeer& Peer = PeerIt->second;
    const uint8 MsgType = Data[0];
    const TArray Payload(Data.begin() + 1, Data.end());

    switch ((EServerMessageType)MsgType)
    {
        case EServerMessageType::MT_ServerHandshake:
        {
            if (!Peer.bAuthenticated)
            {
                SPlayerIdPayload IdPayload;
                auto ParseResult = ParsePayload(Payload, IdPayload, "handshake_minimal");
                if (ParseResult.IsOk() && IdPayload.PlayerId != 0)
                {
                    const uint32 SessionKey = CreateSession(IdPayload.PlayerId, ConnectionId);
                    TArray RespPayload = BuildPayload(SClientLoginResponsePayload{SessionKey, IdPayload.PlayerId});
                    TArray Packet;
                    Packet.reserve(1 + RespPayload.size());
                    Packet.push_back(static_cast<uint8>(EClientMessageType::MT_LoginResponse));
                    Packet.insert(Packet.end(), RespPayload.begin(), RespPayload.end());
                    Peer.Connection->Send(Packet.data(), static_cast<uint32>(Packet.size()));

                    LOG_INFO("Player %llu logged in, session key: %u",
                             (unsigned long long)IdPayload.PlayerId,
                             SessionKey);
                    break;
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
            break;
        }

        case EServerMessageType::MT_Heartbeat:
        {
            SendServerMessage(ConnectionId, EServerMessageType::MT_HeartbeatAck, SEmptyServerMessage{});
            break;
        }

        case EServerMessageType::MT_PlayerLogin:
        {
            if (!Peer.bAuthenticated)
            {
                LOG_WARN("Rejecting player login from unauthenticated connection %llu",
                         (unsigned long long)ConnectionId);
                return;
            }

            SPlayerLoginRequestMessage Request;
            auto ParseResult = ParsePayload(Payload, Request, "MT_PlayerLogin");
            if (!ParseResult.IsOk())
            {
                LOG_WARN("ParsePayload failed: %s", ParseResult.GetError().c_str());
                return;
            }

            const uint32 SessionKey = CreateSession(Request.PlayerId, Request.ConnectionId);

            SendServerMessage(
                ConnectionId,
                EServerMessageType::MT_PlayerLogin,
                SPlayerLoginResponseMessage{Request.ConnectionId, Request.PlayerId, SessionKey});

            LOG_INFO("Player %llu logged in, session key: %u", 
                     (unsigned long long)Request.PlayerId, SessionKey);
            break;
        }

        case EServerMessageType::MT_SessionValidateRequest:
        {
            if (!Peer.bAuthenticated)
            {
                LOG_WARN("Rejecting session validation from unauthenticated connection %llu",
                         (unsigned long long)ConnectionId);
                return;
            }

            SSessionValidateRequestMessage Request;
            auto ParseResult = ParsePayload(Payload, Request, "MT_SessionValidateRequest");
            if (!ParseResult.IsOk())
            {
                LOG_WARN("ParsePayload failed: %s", ParseResult.GetError().c_str());
                return;
            }

            uint64 ValidatedPlayerId = 0;
            const bool bValid = ValidateSession(Request.SessionKey, ValidatedPlayerId) && ValidatedPlayerId == Request.PlayerId;

            SendServerMessage(
                ConnectionId,
                EServerMessageType::MT_SessionValidateResponse,
                SSessionValidateResponseMessage{Request.ConnectionId, Request.PlayerId, bValid});

            LOG_INFO("Session validation for player %llu on connection %llu: %s",
                     (unsigned long long)Request.PlayerId,
                     (unsigned long long)Request.ConnectionId,
                     bValid ? "valid" : "invalid");
            break;
        }

        default:
            break;
    }
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

void MLoginServer::HandleRouterServerMessage(uint8 Type, const TArray& /*Data*/)
{
    if (Type == (uint8)EServerMessageType::MT_ServerRegisterAck)
    {
        LOG_INFO("Login server registered to RouterServer");
    }
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
