#pragma once

#include "../../Core/NetCore.h"
#include "../../Core/Socket.h"
#include "../../Common/Logger.h"
#include "../../Common/ServerConnection.h"
#include <map>
#include <memory>
#include <vector>
#include <thread>
#include <chrono>

// 网关服务器配置
struct FGatewayConfig
{
    uint16 ListenPort = 8001;        // 客户端连接端口
    FString LoginServerAddr = "127.0.0.1";
    uint16 LoginServerPort = 8002;
    FString WorldServerAddr = "127.0.0.1";
    uint16 WorldServerPort = 8003;
};

// 客户端连接
class FClientConnection
{
public:
    uint64 ConnectionId;
    std::shared_ptr<FTcpConnection> Connection;
    uint64 PlayerId = 0;
    bool bAuthenticated = false;
    uint32 SessionToken = 0;
    
    FClientConnection(uint64 Id, std::shared_ptr<FTcpConnection> Conn) 
        : ConnectionId(Id), Connection(Conn) {}
};

// 网关服务器
class FGatewayServer
{
private:
    int32 ListenSocket = -1;
    bool bRunning = false;
    
    // 配置
    FGatewayConfig Config;
    
    // 客户端连接管理
    std::map<uint64, std::shared_ptr<FClientConnection>> ClientConnections;
    uint64 NextConnectionId = 1;
    
    // 与LoginServer的连接 (使用长连接抽象层)
    std::shared_ptr<FServerConnection> LoginServerConn;
    
    // 与WorldServer的连接 (使用长连接抽象层)
    std::shared_ptr<FServerConnection> WorldServerConn;
    
public:
    FGatewayServer() {}
    ~FGatewayServer() { Shutdown(); }
    
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
