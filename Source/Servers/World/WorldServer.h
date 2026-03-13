#pragma once

#include "Core/Net/NetCore.h"
#include "Core/Net/Socket.h"
#include "Core/Net/HttpDebugServer.h"
#include "Common/Logger.h"
#include "Common/NetServerBase.h"
#include "Common/ServerConnection.h"
#include "Common/ServerMessages.h"
#include "NetDriver/NetObject.h"
#include "NetDriver/ReplicationDriver.h"
#include "NetDriver/ReflectionExample.h"
#include "NetDriver/ServerRpcServices.h"
#include <thread>
#include <chrono>

// 世界服务器配置
struct SWorldConfig
{
    uint16 ListenPort = 8003;      // 网关连接端口
    uint16 SceneServerPort = 8004; // 场景服务器端口
    FString RouterServerAddr = "127.0.0.1";
    uint16 RouterServerPort = 8005;
    FString LoginServerAddr = "127.0.0.1";
    uint16 LoginServerPort = 8002;
    FString ServerName = "World01";
    uint32 MaxPlayers = 10000;
    uint16 ZoneId = 0;             // 0 = 默认区
    uint16 DebugHttpPort = 0;      // 调试 HTTP 端口（0 = 关闭）
};

// 玩家数据
struct SPlayer
{
    uint64 PlayerId;
    FString Name;
    uint64 GatewayConnectionId = 0;
    uint32 SessionKey;
    MActor* Character = nullptr;
    // 仅用于 RPC 示例的反射对象
    MHero* HeroObject = nullptr;
    uint32 CurrentSceneId = 0;
    bool bOnline = false;
};

struct SBackendPeer
{
    TSharedPtr<INetConnection> Connection;
    bool bAuthenticated = false;
    uint32 ServerId = 0;
    EServerType ServerType = EServerType::Unknown;
    FString ServerName;
};

struct SPendingSessionValidation
{
    uint64 ValidationRequestId = 0;
    uint64 GatewayConnectionId = 0;
    uint64 PlayerId = 0;
    uint32 SessionKey = 0;
};

// 世界服务器
class MWorldServer : public MNetServerBase
{
private:
    SWorldConfig Config;
    TMap<uint64, SBackendPeer> BackendConnections;

    // 后端服务器连接管理器（Router/Login 等）
    MServerConnectionManager BackendConnectionManager;

    TSharedPtr<MServerConnection> RouterServerConn;
    float LoginRouteQueryTimer = 0.0f;
    float LoadReportTimer = 0.0f;
    uint64 NextRouteRequestId = 1;
    uint64 NextSessionValidationId = 1;
    TSharedPtr<MServerConnection> LoginServerConn;
    TMap<uint64, SPendingSessionValidation> PendingSessionValidations;
    
    // 玩家管理
    TMap<uint64, SPlayer> Players;  // PlayerId -> Player
    
    // 复制系统
    MReplicationDriver* ReplicationDriver;

    // 调试 HTTP 服务器
    TUniquePtr<MHttpDebugServer> DebugServer;

    // World 级 RPC Service（处理跨服务器 RPC）
    MWorldService WorldService;

    // 服务器消息分发器
    MServerMessageDispatcher RouterMessageDispatcher;
    MServerMessageDispatcher LoginMessageDispatcher;
    
public:
    MWorldServer();
    ~MWorldServer() { Shutdown(); }
    
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
    void HandlePacket(uint64 ConnectionId, const TArray& Data);
    void HandleGameplayPacket(uint64 PlayerId, const TArray& Data);
    bool SendServerMessage(uint64 ConnectionId, uint8 Type, const TArray& Payload);
    template<typename TMessage>
    bool SendServerMessage(uint64 ConnectionId, EServerMessageType Type, const TMessage& Message)
    {
        return SendServerMessage(ConnectionId, static_cast<uint8>(Type), BuildPayload(Message));
    }
    void BroadcastToScenes(uint8 Type, const TArray& Payload);
    void HandleRouterServerMessage(uint8 Type, const TArray& Data);
    void SendRouterRegister();
    void QueryLoginServerRoute();
    void SendLoadReport();
    void ApplyLoginServerRoute(uint32 ServerId, const FString& ServerName, const FString& Address, uint16 Port);
    void HandleLoginServerMessage(uint8 Type, const TArray& Data);
    void RequestSessionValidation(uint64 GatewayConnectionId, uint64 PlayerId, uint32 SessionKey);
    
    // 玩家管理
    void AddPlayer(uint64 PlayerId, const FString& Name, uint64 GatewayConnectionId);
    void RemovePlayer(uint64 PlayerId);
    SPlayer* GetPlayerById(uint64 PlayerId);
    
    // 游戏逻辑
    void UpdateGameLogic(float DeltaTime);
    FString BuildDebugStatusJson() const;

    // 分发器注册与具体处理函数
    void InitRouterMessageHandlers();
    void InitLoginMessageHandlers();

    void OnRouter_ServerRegisterAck(const SServerRegisterAckMessage& Message);
    void OnRouter_RouteResponse(const SRouteResponseMessage& Message);
    void OnLogin_SessionValidateResponse(uint64 ConnectionId, uint64 PlayerId, bool bValid);
};
