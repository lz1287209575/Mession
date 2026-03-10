#include "LoginServer.h"
#include <poll.h>
#include <time.h>

FLoginServer::FLoginServer()
{
    std::random_device Rd;
    Rng = std::mt19937(Rd());
}

bool FLoginServer::Init(int InPort)
{
    // 创建监听socket
    ListenSocket = FSocket::CreateListenSocket((uint16)InPort);
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

void FLoginServer::Shutdown()
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
        FSocket::Close(ListenSocket);
        ListenSocket = -1;
    }
    
    LOG_INFO("Login server shutdown complete");
}

void FLoginServer::Tick()
{
    if (!bRunning)
        return;
    
    // 接受新网关连接
    AcceptGateways();
    
    // 处理网关消息
    ProcessGatewayMessages();
    
    // 清理过期会话
    time_t Now = time(nullptr);
    std::vector<uint32> ExpiredSessions;
    
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

void FLoginServer::Run()
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

void FLoginServer::AcceptGateways()
{
    std::string Address;
    uint16 Port;
    
    int32 ClientSocket = FSocket::Accept(ListenSocket, Address, Port);
    
    while (ClientSocket >= 0)
    {
        uint64 ConnectionId = NextConnectionId++;
        auto Connection = std::make_shared<FTcpConnection>(ClientSocket);
        Connection->SetNonBlocking(true);
        
        GatewayConnections[ConnectionId] = Connection;
        
        LOG_INFO("New gateway connected: %s (connection_id=%llu)", 
                 Address.c_str(), (unsigned long long)ConnectionId);
        
        ClientSocket = FSocket::Accept(ListenSocket, Address, Port);
    }
}

void FLoginServer::ProcessGatewayMessages()
{
    std::vector<uint64> DisconnectedGateways;
    
    std::vector<pollfd> PollFds;
    for (auto& [ConnId, Conn] : GatewayConnections)
    {
        if (Conn->IsConnected())
        {
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
            uint8 Buffer[8192];
            uint32 BytesRead = 0;
            
            while (Conn->Receive(Buffer, sizeof(Buffer), BytesRead))
            {
                if (BytesRead > 0)
                {
                    TArray Data(Buffer, Buffer + BytesRead);
                    HandleGatewayPacket(ConnId, Data);
                }
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

void FLoginServer::HandleGatewayPacket(uint64 ConnectionId, const TArray& Data)
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
        
        uint64 PlayerId = *(uint64*)&Data[1];
        
        // 创建会话
        uint32 SessionKey = CreateSession(PlayerId, ConnectionId);
        
        // 发送响应
        TArray Response;
        Response.push_back(2); // LoginResponse
        *(uint32*)&Response[Response.size()] = SessionKey; Response.resize(Response.size() + 4);
        *(uint64*)&Response[Response.size()] = PlayerId; Response.resize(Response.size() + 8);
        
        auto It = GatewayConnections.find(ConnectionId);
        if (It != GatewayConnections.end())
        {
            It->second->Send(Response.data(), Response.size());
        }
        
        LOG_INFO("Player %llu logged in, session key: %u", 
                 (unsigned long long)PlayerId, SessionKey);
    }
}

uint32 FLoginServer::CreateSession(uint64 PlayerId, uint64 ConnectionId)
{
    uint32 SessionKey = GenerateSessionKey();
    
    FSession Session;
    Session.PlayerId = PlayerId;
    Session.SessionKey = SessionKey;
    Session.ConnectionId = ConnectionId;
    Session.ExpireTime = time(nullptr) + 3600; // 1小时过期
    
    Sessions[SessionKey] = Session;
    PlayerSessions[PlayerId] = SessionKey;
    
    return SessionKey;
}

bool FLoginServer::ValidateSession(uint32 SessionKey, uint64& OutPlayerId)
{
    auto It = Sessions.find(SessionKey);
    if (It == Sessions.end())
        return false;
    
    // 检查是否过期
    if (It->second.ExpireTime < time(nullptr))
    {
        RemoveSession(SessionKey);
        return false;
    }
    
    OutPlayerId = It->second.PlayerId;
    return true;
}

void FLoginServer::RemoveSession(uint32 SessionKey)
{
    auto It = Sessions.find(SessionKey);
    if (It != Sessions.end())
    {
        PlayerSessions.erase(It->second.PlayerId);
        Sessions.erase(It);
        LOG_DEBUG("Session %u removed", SessionKey);
    }
}

uint32 FLoginServer::GenerateSessionKey()
{
    std::uniform_int_distribution<uint32> Dist(Config.SessionKeyMin, Config.SessionKeyMax);
    return Dist(Rng);
}
