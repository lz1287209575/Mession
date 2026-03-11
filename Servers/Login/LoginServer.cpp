#include "LoginServer.h"
#include "../../Messages/NetMessages.h"
#include <poll.h>
#include <time.h>

namespace
{
template<typename T>
void AppendValue(TArray& OutData, const T& Value)
{
    const auto* ValueBytes = reinterpret_cast<const uint8*>(&Value);
    OutData.insert(OutData.end(), ValueBytes, ValueBytes + sizeof(T));
}

void AppendString(TArray& OutData, const FString& Value)
{
    const uint16 Length = static_cast<uint16>(Value.size());
    AppendValue(OutData, Length);
    OutData.insert(OutData.end(), Value.begin(), Value.end());
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

MLoginServer::MLoginServer()
{
    std::random_device Rd;
    Rng = std::mt19937(Rd());
}

bool MLoginServer::Init(int InPort)
{
    Config.ListenPort = static_cast<uint16>(InPort);
    MServerConnection::SetLocalInfo(2, EServerType::Login, "Login01");

    // 创建监听socket
    ListenSocket = MSocket::CreateListenSocket((uint16)InPort);
    if (ListenSocket < 0)
    {
        printf("ERROR: Failed to create listen socket on port %d\n", InPort);
        return false;
    }
    
    bRunning = true;
    
    printf("=====================================\n");
    printf("  Mession Login Server\n");
    printf("  Listening on port %d (fd=%d)\n", InPort, ListenSocket);
    printf("=====================================\n");

    SServerConnectionConfig RouterConfig(100, EServerType::Router, "Router01", Config.RouterServerAddr, Config.RouterServerPort);
    RouterServerConn = TSharedPtr<MServerConnection>(new MServerConnection(RouterConfig));
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

void MLoginServer::Shutdown()
{
    if (!bRunning)
        return;
    
    bRunning = false;
    
    // 关闭所有网关连接
    for (auto& [Id, Peer] : GatewayConnections)
    {
        if (Peer.Connection)
            Peer.Connection->Close();
    }
    GatewayConnections.clear();

    if (RouterServerConn)
        RouterServerConn->Disconnect();
    
    // 清理会话
    Sessions.clear();
    PlayerSessions.clear();
    
    // 关闭监听socket
    if (ListenSocket >= 0)
    {
        MSocket::Close(ListenSocket);
        ListenSocket = -1;
    }
    
    LOG_INFO("Login server shutdown complete");
}

void MLoginServer::Tick()
{
    if (!bRunning)
        return;
    
    // 接受新网关连接
    AcceptGateways();
    
    // 处理网关消息
    ProcessGatewayMessages();

    if (RouterServerConn)
        RouterServerConn->Tick(0.016f);
    
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
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

void MLoginServer::AcceptGateways()
{
    TString Address;
    uint16 Port;
    
    int32 ClientSocket = MSocket::Accept(ListenSocket, Address, Port);
    
    while (ClientSocket >= 0)
    {
        uint64 ConnectionId = NextConnectionId++;
        auto Connection = TSharedPtr<MTcpConnection>(new MTcpConnection(ClientSocket));
        Connection->SetNonBlocking(true);

        SGatewayPeer Peer;
        Peer.Connection = Connection;
        GatewayConnections[ConnectionId] = Peer;
        
        LOG_INFO("New gateway connected: %s (connection_id=%llu)", 
                 Address.c_str(), (unsigned long long)ConnectionId);
        
        ClientSocket = MSocket::Accept(ListenSocket, Address, Port);
    }
}

void MLoginServer::ProcessGatewayMessages()
{
    TVector<uint64> DisconnectedGateways;
    
    TVector<pollfd> PollFds;
    for (auto& [ConnId, Peer] : GatewayConnections)
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
    for (auto& [ConnId, Peer] : GatewayConnections)
    {
        if (Index >= PollFds.size())
            break;
        
        if (PollFds[Index].revents & POLLIN)
        {
            TArray Packet;
            while (Peer.Connection->ReceivePacket(Packet))
            {
                HandleGatewayPacket(ConnId, Packet);
            }
            
            if (!Peer.Connection->IsConnected())
            {
                DisconnectedGateways.push_back(ConnId);
            }
        }
        
        Index++;
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
        return;

    auto PeerIt = GatewayConnections.find(ConnectionId);
    if (PeerIt == GatewayConnections.end())
        return;

    SGatewayPeer& Peer = PeerIt->second;
    const uint8 MsgType = Data[0];
    const TArray Payload(Data.begin() + 1, Data.end());

    switch ((EServerMessageType)MsgType)
    {
        case EServerMessageType::MT_ServerHandshake:
        {
            if (!Peer.bAuthenticated &&
                (Payload.size() == sizeof(uint64) || Payload.size() == sizeof(uint64) + 1))
            {
                uint64 PlayerId = 0;
                memcpy(&PlayerId, Payload.data(), sizeof(PlayerId));

                const uint32 SessionKey = CreateSession(PlayerId, ConnectionId);

                TArray Response;
                Response.resize(1 + sizeof(SessionKey) + sizeof(PlayerId));
                Response[0] = (uint8)EClientMessageType::MT_LoginResponse;
                memcpy(Response.data() + 1, &SessionKey, sizeof(SessionKey));
                memcpy(Response.data() + 1 + sizeof(SessionKey), &PlayerId, sizeof(PlayerId));
                Peer.Connection->Send(Response.data(), Response.size());

                LOG_INFO("Player %llu logged in, session key: %u",
                         (unsigned long long)PlayerId,
                         SessionKey);
                break;
            }

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
            LOG_INFO("Gateway %s authenticated (id=%u)",
                     Peer.ServerName.c_str(),
                     Peer.ServerId);
            break;
        }

        case EServerMessageType::MT_Heartbeat:
        {
            SendServerMessage(ConnectionId, (uint8)EServerMessageType::MT_HeartbeatAck, {});
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

            size_t Offset = 0;
            uint64 ClientConnectionId = 0;
            uint64 PlayerId = 0;
            if (!ReadValue(Payload, Offset, ClientConnectionId) ||
                !ReadValue(Payload, Offset, PlayerId))
            {
                LOG_WARN("Invalid player login payload size: %zu", Payload.size());
                return;
            }

            const uint32 SessionKey = CreateSession(PlayerId, ClientConnectionId);

            TArray ResponsePayload;
            AppendValue(ResponsePayload, ClientConnectionId);
            AppendValue(ResponsePayload, PlayerId);
            AppendValue(ResponsePayload, SessionKey);
            SendServerMessage(ConnectionId, (uint8)EServerMessageType::MT_PlayerLogin, ResponsePayload);

            LOG_INFO("Player %llu logged in, session key: %u", 
                     (unsigned long long)PlayerId, SessionKey);
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

            size_t Offset = 0;
            uint64 ClientConnectionId = 0;
            uint64 PlayerId = 0;
            uint32 SessionKey = 0;
            if (!ReadValue(Payload, Offset, ClientConnectionId) ||
                !ReadValue(Payload, Offset, PlayerId) ||
                !ReadValue(Payload, Offset, SessionKey))
            {
                LOG_WARN("Invalid session validation payload size: %zu", Payload.size());
                return;
            }

            uint64 ValidatedPlayerId = 0;
            const bool bValid = ValidateSession(SessionKey, ValidatedPlayerId) && ValidatedPlayerId == PlayerId;

            TArray ResponsePayload;
            AppendValue(ResponsePayload, ClientConnectionId);
            AppendValue(ResponsePayload, PlayerId);
            ResponsePayload.push_back(bValid ? 1 : 0);
            SendServerMessage(ConnectionId, (uint8)EServerMessageType::MT_SessionValidateResponse, ResponsePayload);

            LOG_INFO("Session validation for player %llu on connection %llu: %s",
                     (unsigned long long)PlayerId,
                     (unsigned long long)ClientConnectionId,
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
        return false;

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
        return false;
    
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
        LOG_INFO("Login server registered to RouterServer");
}

void MLoginServer::SendRouterRegister()
{
    if (!RouterServerConn || !RouterServerConn->IsConnected())
        return;

    TArray Payload;
    AppendValue(Payload, static_cast<uint32>(2));
    Payload.push_back((uint8)EServerType::Login);
    AppendString(Payload, "Login01");
    AppendString(Payload, "127.0.0.1");
    AppendValue(Payload, Config.ListenPort);
    RouterServerConn->Send((uint8)EServerMessageType::MT_ServerRegister, Payload.data(), Payload.size());
}
