#pragma once

#include "Core/Net/NetCore.h"
#include "Core/Net/Socket.h"
#include "Core/Net/HttpDebugServer.h"
#include "Common/Logger.h"
#include "Common/NetServerBase.h"
#include "Common/ServerConnection.h"
#include "Common/ServerMessages.h"
#include "Gameplay/PlayerAvatar.h"
#include "NetDriver/NetObject.h"
#include "NetDriver/PersistenceSubsystem.h"
#include "NetDriver/ReplicationDriver.h"
#include "Servers/Login/LoginRpcService.h"
#include "Servers/World/WorldRpcService.h"
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
    bool EnableMgoPersistence = false;
    uint32 OwnerServerId = 3;
};

// 玩家数据
struct SPlayer
{
    uint64 PlayerId;
    FString Name;
    uint64 GatewayConnectionId = 0;
    uint32 SessionKey;
    MPlayerAvatar* Avatar = nullptr;
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

struct SPendingMgoLoad
{
    uint64 RequestId = 0;
    uint64 GatewayConnectionId = 0;
    uint64 PlayerId = 0;
    uint32 SessionKey = 0;
};

struct SPendingMgoPersist
{
    uint64 RequestId = 0;
    uint64 ObjectId = 0;
    uint64 Version = 0;
    double DispatchTime = 0.0;
};

// 世界服务器
MCLASS()
class MWorldServer : public MNetServerBase, public MReflectObject
{
public:
    MGENERATED_BODY(MWorldServer, MReflectObject, 0)

private:
    SWorldConfig Config;
    TMap<uint64, SBackendPeer> BackendConnections;

    // 后端服务器连接管理器（Router/Login 等）
    MServerConnectionManager BackendConnectionManager;

    TSharedPtr<MServerConnection> RouterServerConn;
    float LoginRouteQueryTimer = 0.0f;
    float MgoRouteQueryTimer = 0.0f;
    float LoadReportTimer = 0.0f;
    uint64 NextRouteRequestId = 1;
    uint64 NextSessionValidationId = 1;
    uint64 NextMgoLoadRequestId = 1;
    TSharedPtr<MServerConnection> LoginServerConn;
    TSharedPtr<MServerConnection> MgoServerConn;
    TMap<uint64, SPendingSessionValidation> PendingSessionValidations;
    TMap<uint64, SPendingMgoLoad> PendingMgoLoads;
    TMap<uint64, SPendingMgoPersist> PendingMgoPersists;
    uint64 PersistAckSuccessCount = 0;
    uint64 PersistAckFailedCount = 0;
    uint64 PersistAckTimeoutCount = 0;
    uint64 PersistAckUnmatchedCount = 0;
    
    // 玩家管理
    TMap<uint64, SPlayer> Players;  // PlayerId -> Player
    
    // 复制系统
    MReplicationDriver* ReplicationDriver;
    MPersistenceSubsystem PersistenceSubsystem;

    // 调试 HTTP 服务器
    TUniquePtr<MHttpDebugServer> DebugServer;

    // World 级 RPC Service（处理跨服务器 RPC）
    MWorldService WorldService;

    // 服务器消息分发器
    MServerMessageDispatcher BackendMessageDispatcher;
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
    void Rpc_OnPlayerLoginRequest(uint64 ClientConnectionId, uint64 PlayerId, uint32 SessionKey);
    void Rpc_OnSessionValidateResponse(uint64 ValidationRequestId, uint64 PlayerId, bool bValid);
    void Rpc_OnMgoLoadSnapshotResponse(uint64 RequestId, uint64 ObjectId, bool bFound, uint16 ClassId, const FString& ClassName, const FString& SnapshotHex);
    void Rpc_OnMgoPersistSnapshotResult(uint32 OwnerWorldId, uint64 RequestId, uint64 ObjectId, uint64 Version, bool bSuccess, const FString& Reason);
    void OnPersistRequestDispatched(uint64 RequestId, uint64 ObjectId, uint64 Version);
    MFUNCTION(NetServer, Rpc=ServerToServer, Reliable=true, Endpoint=World)
    void Rpc_OnRouterServerRegisterAck(uint8 Result);
    MFUNCTION(NetServer, Rpc=ServerToServer, Reliable=true, Endpoint=World)
    void Rpc_OnRouterRouteResponse(uint64 RequestId, uint8 RequestedTypeValue, uint64 PlayerId, bool bFound, uint32 ServerId, uint8 ServerTypeValue, const FString& ServerName, const FString& Address, uint16 Port, uint16 ZoneId);

private:
    void HandlePacket(uint64 ConnectionId, const TArray& Data);
    void HandleGameplayPacket(uint64 PlayerId, const TArray& Data);
    bool SendServerMessage(uint64 ConnectionId, uint8 Type, const TArray& Payload);
    bool SendClientFunctionPacketToPlayer(uint64 PlayerId, const TArray& Packet);
    bool SendInventoryPullToPlayer(uint64 PlayerId);
    template<typename TMessage>
    bool SendServerMessage(uint64 ConnectionId, EServerMessageType Type, const TMessage& Message)
    {
        return SendServerMessage(ConnectionId, static_cast<uint8>(Type), BuildPayload(Message));
    }
    void BroadcastToScenes(uint8 Type, const TArray& Payload);
    void HandleRouterServerMessage(uint8 Type, const TArray& Data);
    void SendRouterRegister();
    void QueryLoginServerRoute();
    void QueryMgoServerRoute();
    void SendLoadReport();
    void ApplyLoginServerRoute(uint32 ServerId, const FString& ServerName, const FString& Address, uint16 Port);
    void ApplyMgoServerRoute(uint32 ServerId, const FString& ServerName, const FString& Address, uint16 Port);
    uint64 FindAuthenticatedBackendConnectionId(EServerType ServerType) const;
    void HandleLoginServerMessage(uint8 Type, const TArray& Data);
    void RequestSessionValidation(uint64 GatewayConnectionId, uint64 PlayerId, uint32 SessionKey);
    bool RequestMgoLoad(uint64 PlayerId, uint64 GatewayConnectionId, uint32 SessionKey);
    void FinalizePlayerLogin(uint64 PlayerId, uint64 GatewayConnectionId, uint32 SessionKey, bool bApplyLoadedSnapshot, uint16 LoadedClassId, const FString& LoadedClassName, const FString& LoadedSnapshotHex);
    bool ApplyLoadedSnapshotToPlayer(SPlayer& Player, uint16 ClassId, const FString& ClassName, const FString& SnapshotHex);
    
    // 玩家管理
    void AddPlayer(uint64 PlayerId, const FString& Name, uint64 GatewayConnectionId);
    void RemovePlayer(uint64 PlayerId);
    SPlayer* GetPlayerById(uint64 PlayerId);
    
    // 游戏逻辑
    void UpdateGameLogic(float DeltaTime);
    FString BuildDebugStatusJson() const;

    // 分发器注册与具体处理函数
    void InitBackendMessageHandlers();
    void InitRouterMessageHandlers();
    void InitLoginMessageHandlers();

    void OnBackend_ServerHandshake(uint64 ConnectionId, const SServerHandshakeMessage& Message);
    void OnBackend_Heartbeat(uint64 ConnectionId, const SHeartbeatMessage& Message);
    void OnBackend_PlayerLogin(uint64 ConnectionId, const SPlayerLoginResponseMessage& Message);
    void OnBackend_PlayerLogout(uint64 ConnectionId, const SPlayerLogoutMessage& Message);
    void OnBackend_PlayerClientSync(uint64 ConnectionId, const SPlayerClientSyncMessage& Message);
    void OnRouter_ServerRegisterAck(const SServerRegisterAckMessage& Message);
    void OnRouter_RouteResponse(const SRouteResponseMessage& Message);
    void OnLogin_SessionValidateResponseMessage(const SSessionValidateResponseMessage& Message);
    void OnLogin_SessionValidateResponse(uint64 ConnectionId, uint64 PlayerId, bool bValid);
};
