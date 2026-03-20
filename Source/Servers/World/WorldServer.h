#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/IO/Socket/Socket.h"
#include "Common/Net/HttpDebugServer.h"
#include "Common/Runtime/Log/Logger.h"
#include "Common/Net/NetServerBase.h"
#include "Common/Net/ServerConnection.h"
#include "Protocol/ServerMessages.h"
#include "Common/Runtime/Object/NetObject.h"
#include "Common/Runtime/Persistence/PersistenceSubsystem.h"
#include "Common/Runtime/Replication/ReplicationDriver.h"
#include "Servers/Login/LoginRpcService.h"
#include "Servers/World/WorldConfig.h"
#include "Servers/World/PlayerSession.h"
#include "Servers/World/BackendPeer.h"
#include "Servers/World/PendingRequests.h"
#include "Servers/World/WorldRpcService.h"
#include <thread>
#include <chrono>

// 世界服务器
MCLASS()
class MWorldServer : public MNetServerBase, public MObject
{
public:
    MGENERATED_BODY(MWorldServer, MObject, 0)

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
    TMap<uint64, MPlayerSession> Players;  // PlayerId -> Player
    
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
    
    bool LoadConfig(const MString& ConfigPath);
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
    void Rpc_OnMgoLoadSnapshotResponse(uint64 RequestId, uint64 ObjectId, bool bFound, uint16 ClassId, const MString& ClassName, const MString& SnapshotHex);
    void Rpc_OnMgoPersistSnapshotResult(uint32 OwnerWorldId, uint64 RequestId, uint64 ObjectId, uint64 Version, bool bSuccess, const MString& Reason);
    void OnPersistRequestDispatched(uint64 RequestId, uint64 ObjectId, uint64 Version);
    MFUNCTION(NetServer, Rpc=ServerToServer, Reliable=true, Endpoint=World)
    void Rpc_OnRouterServerRegisterAck(uint8 Result);
    MFUNCTION(NetServer, Rpc=ServerToServer, Reliable=true, Endpoint=World)
    void Rpc_OnRouterRouteResponse(uint64 RequestId, uint8 RequestedTypeValue, uint64 PlayerId, bool bFound, uint32 ServerId, uint8 ServerTypeValue, const MString& ServerName, const MString& Address, uint16 Port, uint16 ZoneId);

private:
    void HandlePacket(uint64 ConnectionId, const TByteArray& Data);
    void HandleGameplayPacket(uint64 PlayerId, const TByteArray& Data);
    bool SendServerMessage(uint64 ConnectionId, uint8 Type, const TByteArray& Payload);
    bool SendClientFunctionPacketToPlayer(uint64 PlayerId, const TByteArray& Packet);
    bool SendInventoryPullToPlayer(uint64 PlayerId);
    template<typename TMessage>
    bool SendServerMessage(uint64 ConnectionId, EServerMessageType Type, const TMessage& Message)
    {
        return SendServerMessage(ConnectionId, static_cast<uint8>(Type), BuildPayload(Message));
    }
    void BroadcastToScenes(uint8 Type, const TByteArray& Payload);
    void HandleRouterServerMessage(uint8 Type, const TByteArray& Data);
    void SendRouterRegister();
    void QueryLoginServerRoute();
    void QueryMgoServerRoute();
    void SendLoadReport();
    void ApplyLoginServerRoute(uint32 ServerId, const MString& ServerName, const MString& Address, uint16 Port);
    void ApplyMgoServerRoute(uint32 ServerId, const MString& ServerName, const MString& Address, uint16 Port);
    uint64 FindAuthenticatedBackendConnectionId(EServerType ServerType) const;
    void HandleLoginServerMessage(uint8 Type, const TByteArray& Data);
    void RequestSessionValidation(uint64 GatewayConnectionId, uint64 PlayerId, uint32 SessionKey);
    bool RequestMgoLoad(uint64 PlayerId, uint64 GatewayConnectionId, uint32 SessionKey);
    void FinalizePlayerLogin(uint64 PlayerId, uint64 GatewayConnectionId, uint32 SessionKey, bool bApplyLoadedSnapshot, uint16 LoadedClassId, const MString& LoadedClassName, const MString& LoadedSnapshotHex);
    bool ApplyLoadedSnapshotToPlayer(MPlayerSession& Player, uint16 ClassId, const MString& ClassName, const MString& SnapshotHex);
    
    // 玩家管理
    void AddPlayer(uint64 PlayerId, const MString& Name, uint64 GatewayConnectionId);
    void RemovePlayer(uint64 PlayerId);
    MPlayerSession* GetPlayerById(uint64 PlayerId);
    
    // 游戏逻辑
    void UpdateGameLogic(float DeltaTime);
    MString BuildDebugStatusJson() const;

    // 分发器注册与具体处理函数
    void InitBackendMessageHandlers();
    void InitRouterMessageHandlers();
    void InitLoginMessageHandlers();

    void OnBackend_ServerHandshake(uint64 ConnectionId, const SNodeHandshakeMessage& Message);
    void OnBackend_Heartbeat(uint64 ConnectionId, const SHeartbeatMessage& Message);
    void OnBackend_PlayerLogin(uint64 ConnectionId, const SPlayerLoginResponseMessage& Message);
    void OnBackend_PlayerLogout(uint64 ConnectionId, const SPlayerLogoutMessage& Message);
    void OnBackend_PlayerClientSync(uint64 ConnectionId, const SPlayerClientSyncMessage& Message);
    void OnRouter_ServerRegisterAck(const SNodeRegisterAckMessage& Message);
    void OnRouter_RouteResponse(const SRouteResponseMessage& Message);
    void OnLogin_SessionValidateResponseMessage(const SSessionValidateResponseMessage& Message);
    void OnLogin_SessionValidateResponse(uint64 ConnectionId, uint64 PlayerId, bool bValid);
};
