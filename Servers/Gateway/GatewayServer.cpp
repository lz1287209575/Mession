#include "GatewayServer.h"
#include <poll.h>

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
    
    WorldServerConn->SetOnConnect([](auto) {
        LOG_INFO("Connected to World Server!");
    });
    WorldServerConn->SetOnAuthenticated([](auto, const SServerInfo& Info) {
        LOG_INFO("World Server authenticated: %s", Info.ServerName.c_str());
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
    
    // 接受新客户端
    AcceptClients();
    
    // 处理客户端消息
    ProcessClientMessages();
    
    // 定期尝试连接后端服务器
    static float ConnectTimer = 0.0f;
    ConnectTimer += 0.1f;
    if (ConnectTimer >= 5.0f)  // 每5秒尝试一次
    {
        ConnectTimer = 0.0f;
        if (LoginServerConn && !LoginServerConn->IsConnected())
        {
            LoginServerConn->Connect();
        }
        if (WorldServerConn && !WorldServerConn->IsConnected())
        {
            WorldServerConn->Connect();
        }
    }
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

void MGatewayServer::HandleClientPacket(uint64 /*ConnectionId*/, const TArray& Data)
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
        // TODO: 转发到LoginServer
        LOG_DEBUG("Forwarding message type %d to LoginServer", MsgType);
    }
    // 游戏消息转发到WorldServer
    else
    {
        // TODO: 转发到WorldServer
        LOG_DEBUG("Forwarding message type %d to WorldServer", MsgType);
    }
}
