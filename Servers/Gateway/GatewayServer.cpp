#include "GatewayServer.h"
#include <poll.h>

namespace
{
template<typename T>
void AppendValue(TArray& OutData, const T& Value)
{
    const auto* ValueBytes = reinterpret_cast<const uint8*>(&Value);
    OutData.insert(OutData.end(), ValueBytes, ValueBytes + sizeof(T));
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

bool MGatewayServer::Init(int InPort)
{
    // 创建监听socket
    ListenSocket = MSocket::CreateListenSocket((uint16)InPort);
    
    if (ListenSocket < 0)
    {
        LOG_ERROR("Failed to create listen socket on port %d", InPort);
        return false;
    }
    
    bRunning = true;
    
    printf("=====================================\n");
    printf("  Mession Gateway Server\n");
    printf("  Listening on port %d (fd=%d)\n", InPort, ListenSocket);
    printf("=====================================\n");
    
    // 设置本服务器信息
    MServerConnection::SetLocalInfo(1, EServerType::Gateway, "Gateway01");
    
    // 初始化后端长连接
    SServerConnectionConfig LoginConfig(2, EServerType::Login, "Login01", "127.0.0.1", 8002);
    LoginServerConn = TSharedPtr<MServerConnection>(new MServerConnection(LoginConfig));
    
    SServerConnectionConfig WorldConfig(3, EServerType::World, "World01", "127.0.0.1", 8003);
    WorldServerConn = TSharedPtr<MServerConnection>(new MServerConnection(WorldConfig));
    
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
    WorldServerConn->SetOnAuthenticated([](auto, const SServerInfo& Info) {
        LOG_INFO("World Server authenticated: %s", Info.ServerName.c_str());
    });
    WorldServerConn->SetOnMessage([this](auto, uint8 Type, const TArray& Data) {
        HandleWorldServerMessage(Type, Data);
    });
    
    // 尝试连接后端服务器
    LoginServerConn->Connect();
    WorldServerConn->Connect();
    
    printf("Backend connections initialized\n");
    
    return true;
}

void MGatewayServer::Shutdown()
{
    if (!bRunning)
        return;
    
    bRunning = false;
    
    // 关闭所有客户端连接
    for (auto& [Id, Conn] : ClientConnections)
    {
        if (Conn->Connection)
            Conn->Connection->Close();
    }
    ClientConnections.clear();
    
    // 关闭后端长连接
    if (LoginServerConn)
        LoginServerConn->Disconnect();
    if (WorldServerConn)
        WorldServerConn->Disconnect();
    
    // 关闭监听socket
    if (ListenSocket >= 0)
    {
        MSocket::Close(ListenSocket);
        ListenSocket = -1;
    }
    
    LOG_INFO("Gateway server shutdown complete");
}

void MGatewayServer::Tick()
{
    if (!bRunning)
        return;

    static constexpr float BackendTickInterval = 0.016f;
    
    // 接受新客户端
    AcceptClients();
    
    if (LoginServerConn)
        LoginServerConn->Tick(BackendTickInterval);
    if (WorldServerConn)
        WorldServerConn->Tick(BackendTickInterval);

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
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

void MGatewayServer::AcceptClients()
{
    TString Address;
    uint16 Port;
    
    int32 ClientSocket = MSocket::Accept(ListenSocket, Address, Port);
    
    while (ClientSocket >= 0)
    {
        uint64 ConnectionId = NextConnectionId++;
        auto Connection = TSharedPtr<MTcpConnection>(new MTcpConnection(ClientSocket));
        Connection->SetNonBlocking(true);
        
        auto Client = TSharedPtr<MClientConnection>(new MClientConnection(ConnectionId, Connection));
        ClientConnections[ConnectionId] = Client;
        
        LOG_INFO("New client connected: %s (connection_id=%llu)", 
                 Address.c_str(), (unsigned long long)ConnectionId);
        
        ClientSocket = MSocket::Accept(ListenSocket, Address, Port);
    }
}

void MGatewayServer::ProcessClientMessages()
{
    TVector<uint64> DisconnectedClients;
    
    TVector<pollfd> PollFds;
    for (auto& [ConnId, Client] : ClientConnections)
    {
        if (Client->Connection->IsConnected())
        {
            Client->Connection->FlushSendBuffer();
            pollfd Pfd;
            Pfd.fd = Client->Connection->GetSocketFd();
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
    for (auto& [ConnId, Client] : ClientConnections)
    {
        if (Index >= PollFds.size())
            break;
        
        if (PollFds[Index].revents & POLLIN)
        {
            TArray Packet;
            while (Client->Connection->ReceivePacket(Packet))
            {
                HandleClientPacket(ConnId, Packet);
            }
            
            if (!Client->Connection->IsConnected())
            {
                DisconnectedClients.push_back(ConnId);
            }
        }
        
        Index++;
    }
    
    // 处理断开连接
    for (uint64 ConnId : DisconnectedClients)
    {
        LOG_INFO("Client disconnected: %llu", (unsigned long long)ConnId);
        ClientConnections.erase(ConnId);
    }
}

void MGatewayServer::HandleClientPacket(uint64 ConnectionId, const TArray& Data)
{
    if (Data.empty())
        return;
    
    // 简单处理：直接转发到后端
    // 实际应该解析消息头，判断类型
    
    // 假设第一个字节是消息类型
    uint8 MsgType = Data[0];
    
    // 登录相关消息转发到LoginServer
    if (MsgType == 1 || MsgType == 3) // Handshake or Login
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
        return;

    if (Data.empty())
        return;

    const uint8 MsgType = Data[0];
    if (MsgType == 1 || MsgType == 3)
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

        TArray Payload;
        AppendValue(Payload, ConnectionId);
        AppendValue(Payload, PlayerId);
        LoginServerConn->Send((uint8)EServerMessageType::MT_PlayerLogin, Payload.data(), Payload.size());
        LOG_DEBUG("Forwarded login request for player %llu", (unsigned long long)PlayerId);
        return;
    }

    if (!ClientIt->second->bAuthenticated)
    {
        LOG_WARN("Ignoring unauthenticated client message type %d", MsgType);
        return;
    }

    if (!WorldServerConn || !WorldServerConn->IsConnected())
    {
        LOG_WARN("World server unavailable, dropping client message type %d", MsgType);
        return;
    }

    TArray Payload;
    AppendValue(Payload, ConnectionId);

    const uint32 DataSize = static_cast<uint32>(Data.size());
    AppendValue(Payload, DataSize);
    Payload.insert(Payload.end(), Data.begin(), Data.end());

    WorldServerConn->Send((uint8)EServerMessageType::MT_PlayerDataSync, Payload.data(), Payload.size());
    LOG_DEBUG("Forwarded client message type %d to WorldServer", MsgType);
}

void MGatewayServer::HandleLoginServerMessage(uint8 Type, const TArray& Data)
{
    if (Type != (uint8)EServerMessageType::MT_PlayerLogin)
        return;

    size_t Offset = 0;
    uint64 ConnectionId = 0;
    uint64 PlayerId = 0;
    uint32 SessionKey = 0;
    if (!ReadValue(Data, Offset, ConnectionId) ||
        !ReadValue(Data, Offset, PlayerId) ||
        !ReadValue(Data, Offset, SessionKey))
    {
        LOG_WARN("Invalid login server payload size: %zu", Data.size());
        return;
    }

    auto ClientIt = ClientConnections.find(ConnectionId);
    if (ClientIt == ClientConnections.end())
    {
        LOG_WARN("Login response for unknown client %llu", (unsigned long long)ConnectionId);
        return;
    }

    auto& Client = ClientIt->second;
    Client->PlayerId = PlayerId;
    Client->SessionToken = SessionKey;
    Client->bAuthenticated = true;

    TArray Response;
    Response.resize(1 + sizeof(SessionKey) + sizeof(PlayerId));
    Response[0] = 2;
    memcpy(Response.data() + 1, &SessionKey, sizeof(SessionKey));
    memcpy(Response.data() + 1 + sizeof(SessionKey), &PlayerId, sizeof(PlayerId));
    Client->Connection->Send(Response.data(), Response.size());

    if (WorldServerConn && WorldServerConn->IsConnected())
    {
        TArray Payload;
        AppendValue(Payload, ConnectionId);
        AppendValue(Payload, PlayerId);
        AppendValue(Payload, SessionKey);
        WorldServerConn->Send((uint8)EServerMessageType::MT_PlayerLogin, Payload.data(), Payload.size());
    }

    LOG_INFO("Client %llu authenticated as player %llu",
             (unsigned long long)ConnectionId,
             (unsigned long long)PlayerId);
}

void MGatewayServer::HandleWorldServerMessage(uint8 Type, const TArray& Data)
{
    if (Type == (uint8)EServerMessageType::MT_PlayerLogout)
    {
        size_t Offset = 0;
        uint64 ConnectionId = 0;
        if (!ReadValue(Data, Offset, ConnectionId))
            return;

        auto ClientIt = ClientConnections.find(ConnectionId);
        if (ClientIt != ClientConnections.end())
        {
            ClientIt->second->bAuthenticated = false;
            ClientIt->second->SessionToken = 0;
        }
    }
}
