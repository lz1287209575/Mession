#pragma once

#include "Core/Net/NetCore.h"
#include "Core/Net/Socket.h"
#include "Core/Net/HttpDebugServer.h"
#include "Common/Logger.h"
#include "Common/NetServerBase.h"
#include "Common/ServerConnection.h"
#include <thread>
#include <chrono>

// 网关服务器配置
struct SGatewayConfig
{
    uint16 ListenPort = 8001;        // 客户端连接端口
    FString RouterServerAddr = "127.0.0.1";
    uint16 RouterServerPort = 8005;
    FString LoginServerAddr = "127.0.0.1";
    uint16 LoginServerPort = 8002;
    FString WorldServerAddr = "127.0.0.1";
    uint16 WorldServerPort = 8003;
    uint16 ZoneId = 0;               // 0 = 任意区
    uint16 DebugHttpPort = 0;        // 调试 HTTP 端口（0 = 关闭）
};

// 客户端连接（通过 INetConnection 抽象）
class MClientConnection
{
public:
    uint64 ConnectionId;
    TSharedPtr<INetConnection> Connection;
    uint64 PlayerId = 0;
    bool bAuthenticated = false;
    uint32 SessionToken = 0;

    MClientConnection(uint64 Id, TSharedPtr<INetConnection> Conn)
        : ConnectionId(Id), Connection(Conn) {}
};

struct SPendingWorldLoginRoute
{
    uint64 ConnectionId = 0;
    uint64 PlayerId = 0;
    uint32 SessionKey = 0;
};

// 网关服务器
class MGatewayServer : public MNetServerBase
{
private:
    SGatewayConfig Config;
    
    // 客户端连接管理
    TMap<uint64, TSharedPtr<MClientConnection>> ClientConnections;
    uint64 NextConnectionId = 1;
    
    // 后端服务器连接管理器（Router/Login/World 等）
    MServerConnectionManager BackendConnectionManager;

    // 调试 HTTP 服务器
    TUniquePtr<MHttpDebugServer> DebugServer;

    // 与LoginServer的连接 (使用长连接抽象层)
    TSharedPtr<MServerConnection> LoginServerConn;
    
    // 与WorldServer的连接 (使用长连接抽象层)
    TSharedPtr<MServerConnection> WorldServerConn;

    // 与RouterServer的连接 (控制面)
    TSharedPtr<MServerConnection> RouterServerConn;
    float RouteQueryTimer = 0.0f;
    uint64 NextRouteRequestId = 1;
    TMap<uint64, SPendingWorldLoginRoute> PendingWorldLoginRoutes;
    
public:
    MGatewayServer() {}
    ~MGatewayServer() { Shutdown(); }
    
    bool LoadConfig(const FString& ConfigPath);
    bool Init(int InPort = 0);
    void Tick();
    void Run() override { MNetServerBase::Run(); }

    uint16 GetListenPort() const override;
    void OnAccept(uint64 ConnId, TSharedPtr<INetConnection> Conn) override;
    void TickBackends() override;
    void ShutdownConnections() override;
    void OnRunStarted() override;

private:
    void ConnectToLoginServer();
    void ConnectToWorldServer();
    void ConnectToRouterServer();
    TSharedPtr<MClientConnection> FindClientByPlayerId(uint64 PlayerId);
    void ResetClientAuthState(const TSharedPtr<MClientConnection>& Client);
    void HandleClientPacket(uint64 ConnectionId, const TArray& Data);
    void ForwardToBackend(uint64 ConnectionId, const TArray& Data);
    void HandleLoginServerMessage(uint8 Type, const TArray& Data);
    void HandleWorldServerMessage(uint8 Type, const TArray& Data);
    void HandleRouterServerMessage(uint8 Type, const TArray& Data);
    void SendRouterRegister();
    uint64 QueryRoute(EServerType ServerType, uint64 PlayerId = 0);
    void ApplyRoute(EServerType ServerType, uint32 ServerId, const FString& ServerName, const FString& Address, uint16 Port);
    void FlushPendingWorldLogins();
    FString BuildDebugStatusJson() const;
};
