#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/IO/Socket/Socket.h"
#include "Common/Net/HttpDebugServer.h"
#include "Common/Runtime/Log/Logger.h"
#include "Common/Net/NetServerBase.h"
#include "Common/Net/ServerConnection.h"
#include "Protocol/ServerMessages.h"
#include "Common/Runtime/Object/Object.h"
#include "Common/Runtime/Object/NetObject.h"
#include "Common/Runtime/Persistence/PersistenceSubsystem.h"
#include "Common/Runtime/Replication/ReplicationDriver.h"
#include "Servers/World/WorldConfig.h"
#include "Servers/World/PlayerSession.h"
#include "Servers/World/BackendPeer.h"
#include "Servers/World/PendingRequests.h"
#include <thread>
#include <chrono>

class MPlayerAvatar;

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
    TMap<uint64, MPlayerAvatar*> Players;  // PlayerId -> Avatar root
    
    // 复制系统
    MReplicationDriver* ReplicationDriver;
    MPersistenceSubsystem PersistenceSubsystem;

    // 调试 HTTP 服务器
    TUniquePtr<MHttpDebugServer> DebugServer;

public:
    MWorldServer();
    ~MWorldServer() { Shutdown(); }
    using MObject::Tick;
    
    bool LoadConfig(const MString& ConfigPath);
    bool Init(int InPort = 0);
    void Tick();
    void Run() override { MNetServerBase::Run(); }

    uint16 GetListenPort() const override;
    void OnAccept(uint64 ConnId, TSharedPtr<INetConnection> Conn) override;
    void TickBackends() override;
    void ShutdownConnections() override;
    void OnRunStarted() override;
    MFUNCTION(NetServer, Rpc=ServerToServer, Reliable=true, Endpoint=World)
    void Rpc_OnServerHandshake(uint32 ServerId, uint8 ServerTypeValue, const MString& ServerName);
    MFUNCTION(NetServer, Rpc=ServerToServer, Reliable=true, Endpoint=World)
    void Rpc_OnHeartbeat(uint32 Sequence);
    MFUNCTION(NetServer, Rpc=ServerToServer, Reliable=true, Endpoint=World)
    void Rpc_OnPlayerLoginRequest(uint64 ClientConnectionId, uint64 PlayerId, uint32 SessionKey);
    MFUNCTION(NetServer, Rpc=ServerToServer, Reliable=true, Endpoint=World)
    void Rpc_OnSessionValidateResponse(uint64 ValidationRequestId, uint64 PlayerId, bool bValid);
    MFUNCTION(NetServer, Rpc=ServerToServer, Reliable=true, Endpoint=World)
    void Rpc_OnMgoLoadSnapshotResponse(uint64 RequestId, uint64 ObjectId, bool bFound, uint16 ClassId, const MString& ClassName, const MString& SnapshotHex);
    MFUNCTION(NetServer, Rpc=ServerToServer, Reliable=true, Endpoint=World)
    void Rpc_OnMgoPersistSnapshotResult(uint32 OwnerWorldId, uint64 RequestId, uint64 ObjectId, uint64 Version, bool bSuccess, const MString& Reason);
    void OnPersistRequestDispatched(uint64 RequestId, uint64 ObjectId, uint64 Version);
    MFUNCTION(NetServer, Rpc=ServerToServer, Reliable=true, Endpoint=World)
    void Rpc_OnRouterServerRegisterAck(uint8 Result);
    MFUNCTION(NetServer, Rpc=ServerToServer, Reliable=true, Endpoint=World)
    void Rpc_OnRouterRouteResponse(uint64 RequestId, uint8 RequestedTypeValue, uint64 PlayerId, bool bFound, uint32 ServerId, uint8 ServerTypeValue, const MString& ServerName, const MString& Address, uint16 Port, uint16 ZoneId);
    MFUNCTION(NetServer, Rpc=ServerToServer, Reliable=true, Endpoint=World)
    void Rpc_OnPlayerLogout(uint64 PlayerId);
    MFUNCTION(NetServer, Rpc=ServerToServer, Reliable=true, Endpoint=World)
    void Rpc_OnPlayerClientSync(uint64 PlayerId, const MString& PacketHex);

private:
    void HandlePacket(uint64 ConnectionId, const TByteArray& Data);
    void HandleGameplayPacket(uint64 PlayerId, const TByteArray& Data);
    bool SendServerPacket(uint64 ConnectionId, uint8 PacketType, const TByteArray& Payload);
    bool SendClientFunctionPacketToPlayer(uint64 PlayerId, const TByteArray& Packet);
    bool SendInventoryPullToPlayer(uint64 PlayerId);
    template<typename TMessage>
    bool SendServerPacket(uint64 ConnectionId, EServerMessageType PacketType, const TMessage& Message)
    {
        return SendServerPacket(ConnectionId, static_cast<uint8>(PacketType), BuildPayload(Message));
    }
    void HandleRouterServerPacket(uint8 PacketType, const TByteArray& Data);
    void SendRouterRegister();
    void QueryLoginServerRoute();
    void QueryMgoServerRoute();
    void SendLoadReport();
    void ApplyLoginServerRoute(uint32 ServerId, const MString& ServerName, const MString& Address, uint16 Port);
    void ApplyMgoServerRoute(uint32 ServerId, const MString& ServerName, const MString& Address, uint16 Port);
    uint64 FindAuthenticatedBackendConnectionId(EServerType ServerType) const;
    void HandleLoginServerPacket(uint8 PacketType, const TByteArray& Data);
    void RequestSessionValidation(uint64 GatewayConnectionId, uint64 PlayerId, uint32 SessionKey);
    bool RequestMgoLoad(uint64 PlayerId, uint64 GatewayConnectionId, uint32 SessionKey);
    void BeginLoadPlayerState(uint64 PlayerId, uint64 GatewayConnectionId, uint32 SessionKey);
    void CompletePlayerLogin(uint64 PlayerId, uint64 GatewayConnectionId, uint32 SessionKey, bool bApplyLoadedSnapshot, uint16 LoadedClassId, const MString& LoadedClassName, const MString& LoadedSnapshotHex);
    bool ApplyLoadedSnapshotToPlayer(MPlayerAvatar* PlayerAvatar, uint16 ClassId, const MString& ClassName, const MString& SnapshotHex);
    
    // 玩家管理
    MPlayerAvatar* CreateRuntimePlayer(uint64 PlayerId, uint64 GatewayConnectionId, uint32 SessionKey);
    void EnterWorld(MPlayerAvatar* PlayerAvatar);
    void RemovePlayer(uint64 PlayerId);
    MPlayerSession* GetPlayerById(uint64 PlayerId);
    MPlayerAvatar* GetPlayerAvatarById(uint64 PlayerId);
    
    // 游戏逻辑
    void UpdateGameLogic(float DeltaTime);
    MString BuildDebugStatusJson() const;

    void OnRouter_ServerRegisterAck(const SNodeRegisterAckMessage& Message);
    void OnRouter_RouteResponse(const SRouteResponseMessage& Message);
    void OnLogin_SessionValidateResponse(uint64 ConnectionId, uint64 PlayerId, bool bValid);
};
