#pragma once

#include "../Core/NetCore.h"
#include "../Core/Socket.h"
#include "../NetDriver/ReplicationDriver.h"
#include "../Messages/NetMessages.h"
#include <thread>
#include <chrono>
#include <poll.h>

// 游戏服务器主类
class AGameServer : public IMessageHandler
{
private:
    // 服务器配置
    uint16 Port = 7777;
    int32 ListenSocket = -1;
    bool bRunning = false;
    
    // 网络
    std::map<uint64, std::shared_ptr<FTcpConnection>> Connections;
    uint64 NextConnectionId = 1;
    
    // 复制系统
    UReplicationDriver* ReplicationDriver;
    
    // 消息分发
    FMessageDispatcher* MessageDispatcher;
    
    // 玩家数据
    struct FPlayerData
    {
        uint64 PlayerId;
        FString Name;
        uint64 ConnectionId;
        AActor* Character = nullptr;
        bool bAuthenticated = false;
        float LastHeartbeatTime = 0.0f;
    };
    std::map<uint64, FPlayerData> Players;
    
    // 时间
    double ServerStartTime = 0.0f;
    
public:
    AGameServer()
    {
        ReplicationDriver = new UReplicationDriver();
        MessageDispatcher = new FMessageDispatcher(this);
    }
    
    ~AGameServer()
    {
        Shutdown();
        delete ReplicationDriver;
        delete MessageDispatcher;
    }
    
    // 启动服务器
    bool Start(uint16 InPort = 7777)
    {
        Port = InPort;
        
        // 创建监听socket
        ListenSocket = FSocket::CreateListenSocket(Port);
        if (ListenSocket < 0)
        {
            LOG_ERROR("Failed to start server on port %d", Port);
            return false;
        }
        
        bRunning = true;
        ServerStartTime = GetCurrentTime();
        
        LOG_INFO("======================================");
        LOG_INFO("  MMO Server Started on port %d", Port);
        LOG_INFO("======================================");
        
        return true;
    }
    
    // 关闭服务器
    void Shutdown()
    {
        if (!bRunning)
            return;
        
        bRunning = false;
        
        // 关闭所有连接
        for (auto& [Id, Conn] : Connections)
        {
            Conn->Close();
        }
        Connections.clear();
        Players.clear();
        
        // 关闭监听socket
        if (ListenSocket >= 0)
        {
            FSocket::Close(ListenSocket);
            ListenSocket = -1;
        }
        
        LOG_INFO("Server shutdown complete");
    }
    
    // 主循环
    void Tick()
    {
        if (!bRunning)
            return;
        
        // 1. 处理新连接
        AcceptNewConnections();
        
        // 2. 处理网络消息
        ProcessNetworkMessages();
        
        // 3. 复制更新
        ReplicationDriver->Tick(DEFAULT_TICK_RATE);
        
        // 4. 心跳检查
        CheckHeartbeats();
        
        // 5. 游戏逻辑更新
        UpdateGameLogic(DEFAULT_TICK_RATE);
    }
    
    // 运行服务器（阻塞）
    void Run()
    {
        if (!bRunning)
        {
            LOG_ERROR("Server not started!");
            return;
        }
        
        LOG_INFO("Server running... Press Ctrl+C to stop");
        
        while (bRunning)
        {
            Tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60 FPS
        }
    }

private:
    // 接受新连接
    void AcceptNewConnections()
    {
        std::string Address;
        uint16 PortNum;
        
        int32 ClientSocket = FSocket::Accept(ListenSocket, Address, PortNum);
        
        while (ClientSocket >= 0)
        {
            uint64 ConnectionId = NextConnectionId++;
            auto Connection = std::make_shared<FTcpConnection>(ClientSocket);
            Connection->SetPlayerId(ConnectionId);
            Connection->SetNonBlocking(true);
            
            Connections[ConnectionId] = Connection;
            ReplicationDriver->AddConnection(ConnectionId, Connection);
            
            LOG_INFO("New connection: %s (connection_id=%llu)", 
                     Address.c_str(), (unsigned long long)ConnectionId);
            
            // 继续接受下一个
            ClientSocket = FSocket::Accept(ListenSocket, Address, PortNum);
        }
    }
    
    // 处理网络消息
    void ProcessNetworkMessages()
    {
        std::vector<uint64> DisconnectedConns;
        
        // 准备pollfd
        std::vector<pollfd> PollFds;
        for (auto& [ConnId, Conn] : Connections)
        {
            if (Conn->IsConnected())
            {
                pollfd Pfd;
                Pfd.fd = Conn->GetSocketFd();
                Pfd.events = POLLIN;
                Pfd.revents = 0;
                PollFds.push_back(Pfd);
            }
        }
        
        if (PollFds.empty())
            return;
        
        // 等待100ms
        int32 Ret = poll(PollFds.data(), PollFds.size(), 100);
        
        if (Ret < 0)
        {
            if (errno != EINTR)
                LOG_ERROR("Poll error: %s", strerror(errno));
            return;
        }
        
        // 处理可读socket
        size_t Index = 0;
        for (auto& [ConnId, Conn] : Connections)
        {
            if (Index >= PollFds.size())
                break;
            
            if (PollFds[Index].revents & POLLIN)
            {
                // 接收数据
                uint8 Buffer[8192];
                uint32 BytesRead = 0;
                
                while (Conn->Receive(Buffer, sizeof(Buffer), BytesRead))
                {
                    // 简单处理：直接分发
                    if (BytesRead > 0)
                    {
                        TArray Data(Buffer, Buffer + BytesRead);
                        MessageDispatcher->Dispatch(ConnId, Data);
                    }
                }
                
                if (!Conn->IsConnected())
                {
                    DisconnectedConns.push_back(ConnId);
                }
            }
            
            Index++;
        }
        
        // 处理断开的连接
        for (uint64 ConnId : DisconnectedConns)
        {
            HandleDisconnect(ConnId);
        }
    }
    
    // 处理断开连接
    void HandleDisconnect(uint64 ConnectionId)
    {
        auto ConnIt = Connections.find(ConnectionId);
        if (ConnIt != Connections.end())
        {
            LOG_INFO("Connection %llu disconnected", (unsigned long long)ConnectionId);
            
            // 如果玩家已登录，保存数据
            auto PlayerIt = Players.find(ConnectionId);
            if (PlayerIt != Players.end())
            {
                // 广播玩家离开
                if (PlayerIt->second.Character)
                {
                    ReplicationDriver->BroadcastActorDestroy(
                        PlayerIt->second.Character->GetObjectId()
                    );
                }
                Players.erase(PlayerIt);
            }
            
            // 清理连接
            ReplicationDriver->RemoveConnection(ConnectionId);
            Connections.erase(ConnIt);
        }
    }
    
    // 心跳检查
    void CheckHeartbeats()
    {
        double CurrentTime = GetCurrentTime();
        
        for (auto& [PlayerId, Player] : Players)
        {
            if (CurrentTime - Player.LastHeartbeatTime > 30.0f)
            {
                LOG_WARN("Player %s (%llu) heartbeat timeout", 
                         Player.Name.c_str(), (unsigned long long)Player.PlayerId);
                HandleDisconnect(Player.ConnectionId);
            }
        }
    }
    
    // 游戏逻辑更新
    void UpdateGameLogic(float DeltaTime)
    {
        // 这里更新游戏逻辑
    }
    
    // 获取当前时间（秒）
    double GetCurrentTime()
    {
        auto Now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double>(Now.time_since_epoch()).count();
    }

public:
    // IMessageHandler接口实现
    void OnHandshake(uint64 ConnectionId, const FHandshakeMessage& Msg) override
    {
        LOG_DEBUG("Handshake from connection %llu: version=%d, name=%s",
                  (unsigned long long)ConnectionId, 
                  Msg.ProtocolVersion, 
                  Msg.ClientName.c_str());
        
        // 简单处理：直接允许连接
        // 实际应该验证版本号
    }
    
    void OnLogin(uint64 ConnectionId, const FLoginMessage& Msg) override
    {
        LOG_INFO("Login attempt: player=%s, token=%s", 
                 Msg.PlayerName.c_str(), Msg.Token.c_str());
        
        auto ConnIt = Connections.find(ConnectionId);
        if (ConnIt == Connections.end())
            return;
        
        // 创建玩家数据
        uint64 PlayerId = Msg.PlayerId > 0 ? Msg.PlayerId : ConnectionId;
        
        FPlayerData Player;
        Player.PlayerId = PlayerId;
        Player.Name = Msg.PlayerName.empty() ? "Player" : Msg.PlayerName;
        Player.ConnectionId = ConnectionId;
        Player.bAuthenticated = true;
        Player.LastHeartbeatTime = GetCurrentTime();
        
        Players[ConnectionId] = Player;
        
        // 创建玩家角色（简单示例）
        AActor* Character = new AActor();
        Character->SetReplicated(true);
        Character->SetActorReplicates(true);
        Character->SetActorActive(true);
        Character->SetLocation(FVector(0, 0, 100));
        
        Player.Character = Character;
        
        // 注册到复制系统
        ReplicationDriver->RegisterActor(Character);
        ReplicationDriver->AddRelevantActor(ConnectionId, Character->GetObjectId());
        
        // 广播给其他玩家
        ReplicationDriver->BroadcastActorCreate(Character, ConnectionId);
        
        // 发送登录响应
        FLoginResponseMessage Response;
        Response.Result = 0; // 成功
        Response.AssignedPlayerId = PlayerId;
        Response.Message = "Welcome to MMO Server!";
        
        // 发送响应
        TArray Data;
        FMemoryArchive Ar(Data);
        uint8 MsgType = (uint8)ENetMessageType::MT_LoginResponse;
        Ar << MsgType;
        Response.Serialize(Ar);
        
        ConnIt->second->Send(Ar.GetData().data(), Ar.GetData().size());
        
        LOG_INFO("Player %s logged in (player_id=%llu)", 
                 Player.Name.c_str(), (unsigned long long)PlayerId);
    }
    
    void OnLogout(uint64 ConnectionId) override
    {
        LOG_INFO("Logout: connection=%llu", (unsigned long long)ConnectionId);
        HandleDisconnect(ConnectionId);
    }
    
    void OnActorUpdate(uint64 ConnectionId, uint64 ActorId, const TArray& Data) override
    {
        // 处理客户端发来的Actor更新（谨慎处理，防止作弊）
        LOG_DEBUG("Actor update: connection=%llu, actor=%llu, size=%u",
                  (unsigned long long)ConnectionId, 
                  (unsigned long long)ActorId, 
                  (uint32)Data.size());
        
        // 实际应该验证权限，然后更新服务器端状态
    }
    
    void OnHeartbeat(uint64 ConnectionId) override
    {
        auto It = Players.find(ConnectionId);
        if (It != Players.end())
        {
            It->second.LastHeartbeatTime = GetCurrentTime();
        }
    }
    
    void OnError(uint64 ConnectionId, uint16 ErrorCode, const FString& ErrorMsg) override
    {
        LOG_ERROR("Error from %llu: [%d] %s", 
                  (unsigned long long)ConnectionId, 
                  ErrorCode, 
                  ErrorMsg.c_str());
    }
};
