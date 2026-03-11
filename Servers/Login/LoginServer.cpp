#include "LoginServer.h"
#include <poll.h>
#include <time.h>

MLoginServer::MLoginServer()
{
    std::random_device Rd;
    Rng = std::mt19937(Rd());
}

bool MLoginServer::Init(int InPort)
{
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
    
    return true;
}

void MLoginServer::Shutdown()
{
    if (!bRunning)
        return;
    
    bRunning = false;
    
    // 关闭所有网关连接
    for (auto& [Id, Conn] : GatewayConnections)
    {
        Conn->Close();
    }
    GatewayConnections.clear();
    
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
        
        GatewayConnections[ConnectionId] = Connection;
        
        LOG_INFO("New gateway connected: %s (connection_id=%llu)", 
                 Address.c_str(), (unsigned long long)ConnectionId);
        
        ClientSocket = MSocket::Accept(ListenSocket, Address, Port);
    }
}

void MLoginServer::ProcessGatewayMessages()
{
    TVector<uint64> DisconnectedGateways;
    
    TVector<pollfd> PollFds;
    for (auto& [ConnId, Conn] : GatewayConnections)
    {
        if (Conn->IsConnected())
        {
            Conn->FlushSendBuffer();
            pollfd Pfd;
            Pfd.fd = Conn->GetSocketFd();
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
    for (auto& [ConnId, Conn] : GatewayConnections)
    {
        if (Index >= PollFds.size())
            break;
        
        if (PollFds[Index].revents & POLLIN)
        {
            TArray Packet;
            while (Conn->ReceivePacket(Packet))
            {
                HandleGatewayPacket(ConnId, Packet);
            }
            
            if (!Conn->IsConnected())
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
    if (Data.empty() || Data.size() < 2)
        return;
    
    // 解析消息
    // 格式: [MsgType(1)][PlayerId(8)][PlayerNameLen(2)][PlayerName...]
    uint8 MsgType = Data[0];
    
    if (MsgType == 1) // Login request
    {
        if (Data.size() < 10)
            return;

        uint64 PlayerId = 0;
        memcpy(&PlayerId, Data.data() + 1, sizeof(PlayerId));
        
        // 创建会话
        uint32 SessionKey = CreateSession(PlayerId, ConnectionId);
        
        // 发送响应
        TArray Response;
        Response.resize(1 + sizeof(SessionKey) + sizeof(PlayerId));
        Response[0] = 2; // LoginResponse
        memcpy(Response.data() + 1, &SessionKey, sizeof(SessionKey));
        memcpy(Response.data() + 1 + sizeof(SessionKey), &PlayerId, sizeof(PlayerId));
        
        auto It = GatewayConnections.find(ConnectionId);
        if (It != GatewayConnections.end())
        {
            It->second->Send(Response.data(), Response.size());
        }
        
        LOG_INFO("Player %llu logged in, session key: %u", 
                 (unsigned long long)PlayerId, SessionKey);
    }
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
