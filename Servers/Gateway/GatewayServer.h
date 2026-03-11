#pragma once

#include "../../Core/NetCore.h"
#include "../../Core/Socket.h"
#include "../../Common/Logger.h"
#include "../../Common/ServerConnection.h"
#include <thread>
#include <chrono>

// 网关服务器配置
struct SGatewayConfig
{
    uint16 ListenPort = 8001;        // 客户端连接端口
    FString LoginServerAddr = "127.0.0.1";
    uint16 LoginServerPort = 8002;
    FString WorldServerAddr = "127.0.0.1";
    uint16 WorldServerPort = 8003;
};

// 客户端连接
class MClientConnection
{
public:
    uint64 ConnectionId;
    TSharedPtr<MTcpConnection> Connection;
    uint64 PlayerId = 0;
    bool bAuthenticated = false;
    uint32 SessionToken = 0;
    
    MClientConnection(uint64 Id, TSharedPtr<MTcpConnection> Conn) 
        : ConnectionId(Id), Connection(Conn) {}
};

// 网关服务器
class MGatewayServer
{
private:
    int32 ListenSocket = -1;
    bool bRunning = false;
    
    // 配置
    SGatewayConfig Config;
    
    // 客户端连接管理
    TMap<uint64, TSharedPtr<MClientConnection>> ClientConnections;
    uint64 NextConnectionId = 1;
    
    // 与LoginServer的连接 (使用长连接抽象层)
    TSharedPtr<MServerConnection> LoginServerConn;
    
    // 与WorldServer的连接 (使用长连接抽象层)
    TSharedPtr<MServerConnection> WorldServerConn;
    
public:
    MGatewayServer() {}
    ~MGatewayServer() { Shutdown(); }
    
    bool Init(int InPort);
    void Shutdown();
    void Tick();
    void Run();
    
private:
    void AcceptClients();
    void ProcessClientMessages();
    void ConnectToLoginServer();
    void ConnectToWorldServer();
    void HandleClientPacket(uint64 ConnectionId, const TArray& Data);
    void ForwardToBackend(uint64 ConnectionId, const TArray& Data);
};
