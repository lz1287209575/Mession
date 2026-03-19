#pragma once

#include "Common/MLib.h"
#include "Core/Net/Socket.h"
#include "Core/Net/HttpDebugServer.h"
#include "Common/Logger.h"
#include "Common/NetServerBase.h"
#include "Common/ServerConnection.h"
#include "Common/ServerMessages.h"
#include "Messages/NetMessages.h"
#include "Servers/Gateway/GatewayRpcService.h"
#include "Servers/Login/LoginRpcService.h"
#include "Servers/World/WorldRpcService.h"
#include <thread>
#include <chrono>

// 网关服务器配置
struct SGatewayConfig
{
    uint16 ListenPort = 8001;        // 客户端连接端口
    MString RouterServerAddr = "127.0.0.1";
    uint16 RouterServerPort = 8005;
    MString LoginServerAddr = "127.0.0.1";
    uint16 LoginServerPort = 8002;
    MString WorldServerAddr = "127.0.0.1";
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
    uint64 HandshakeCount = 0;
    uint64 LastHandshakePlayerId = 0;
    uint64 HeartbeatCount = 0;
    uint32 LastHeartbeatSequence = 0;

    MClientConnection(uint64 Id, TSharedPtr<INetConnection> Conn)
        : ConnectionId(Id), Connection(Conn) {}
};

struct SPendingWorldLoginRoute
{
    uint64 ConnectionId = 0;
    uint64 PlayerId = 0;
    uint32 SessionKey = 0;
};

struct SPendingResolvedClientRoute
{
    uint64 RequestId = 0;
    uint64 ConnectionId = 0;
    uint64 PlayerId = 0;
    EServerType TargetServerType = EServerType::Unknown;
    MString WrapMode;
    TByteArray Packet;
};

// 网关服务器
MCLASS()
class MGatewayServer : public MNetServerBase, public MReflectObject, public IGeneratedClientRouteTarget
{
public:
    MGENERATED_BODY(MGatewayServer, MReflectObject, 0)

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
    TMap<uint64, TVector<SPendingResolvedClientRoute>> PendingResolvedClientRoutes;
    TMap<MString, uint64> InFlightResolvedRouteRequests;
    TMap<MString, SServerInfo> ResolvedRouteCache;
    uint64 ClientFunctionCallCount = 0;
    uint64 ClientFunctionCallRejectedCount = 0;
    uint64 UnknownClientFunctionCount = 0;
    uint64 ClientFunctionDecodeFailureCount = 0;
    uint16 LastClientFunctionId = 0;
    MString LastClientFunctionName;
    MString LastClientFunctionError;

    MGatewayService GatewayService;

    // 服务器消息分发器
    MServerMessageDispatcher LoginMessageDispatcher;
    MServerMessageDispatcher WorldMessageDispatcher;
    MServerMessageDispatcher RouterMessageDispatcher;
    
public:
    MGatewayServer() {}
    ~MGatewayServer() { Shutdown(); }
    
    bool LoadConfig(const MString& ConfigPath);
    bool Init(int InPort = 0);
    void Tick();
    void Run() override { MNetServerBase::Run(); }

    uint16 GetListenPort() const override;
    void OnAccept(uint64 ConnId, TSharedPtr<INetConnection> Conn) override;
    void TickBackends() override;
    void ShutdownConnections() override;
    void OnRunStarted() override;
    MFUNCTION(Client, Message=MT_Handshake, Reliable=true)
    void Client_Handshake(uint64 ClientConnectionId, const SPlayerIdPayload& Request);
    MFUNCTION(Client, Message=MT_Login, Reliable=true, Route=Login, Auth=None, Wrap=LoginRpcOrLegacy)
    void Client_Login(uint64 ClientConnectionId, const SPlayerIdPayload& Request);
    MFUNCTION(Client, Message=MT_PlayerMove, Reliable=false, Route=RouterResolved, Target=World, Auth=Required, Wrap=PlayerClientSync)
    void Client_PlayerMove(uint64 ClientConnectionId, const SPlayerMovePayload& MovePayload);
    MFUNCTION(Client, Message=MT_Chat, Reliable=true, Route=RouterResolved, Target=World, Auth=Required, Wrap=PlayerClientSync)
    void Client_Chat(uint64 ClientConnectionId, const SClientChatPayload& ChatPayload);
    MFUNCTION(Client, Message=MT_Heartbeat, Reliable=false)
    void Client_Heartbeat(uint64 ClientConnectionId, const SHeartbeatMessage& Heartbeat);
    void Rpc_OnPlayerLoginResponse(uint64 ClientConnectionId, uint64 PlayerId, uint32 SessionKey);
    MFUNCTION(NetServer, Rpc=ServerToServer, Reliable=true, Endpoint=Gateway)
    void Rpc_OnRouterServerRegisterAck(uint8 Result);
    MFUNCTION(NetServer, Rpc=ServerToServer, Reliable=true, Endpoint=Gateway)
    void Rpc_OnRouterRouteResponse(uint64 RequestId, uint8 RequestedTypeValue, uint64 PlayerId, bool bFound, uint32 ServerId, uint8 ServerTypeValue, const MString& ServerName, const MString& Address, uint16 Port, uint16 ZoneId);

private:
    void ConnectToLoginServer();
    void ConnectToWorldServer();
    void ConnectToRouterServer();
    TSharedPtr<MClientConnection> FindClientByPlayerId(uint64 PlayerId);
    void ResetClientAuthState(const TSharedPtr<MClientConnection>& Client);
    bool IsGeneratedRouteAuthorized(const TSharedPtr<MClientConnection>& Client, const SGeneratedClientRouteRequest& Request) const;
    TByteArray BuildGeneratedRoutePacket(const SGeneratedClientRouteRequest& Request) const;
    TSharedPtr<MServerConnection> ResolveGeneratedRouteConnection(SGeneratedClientRouteRequest::ERouteKind RouteKind) const;
    TSharedPtr<MServerConnection> ResolveGeneratedRouteConnection(EServerType TargetServerType) const;
    bool EnsureGeneratedRouteResolved(const SGeneratedClientRouteRequest& Request, const TSharedPtr<MClientConnection>& Client);
    EGeneratedClientDispatchResult ExecuteGeneratedRouteRawToConnection(const TSharedPtr<MServerConnection>& Connection, SGeneratedClientRouteRequest::ERouteKind RouteKind, const TByteArray& Packet);
    EGeneratedClientDispatchResult ExecuteGeneratedRouteRaw(const TSharedPtr<MClientConnection>& Client, const SGeneratedClientRouteRequest& Request, const TByteArray& Packet);
    EGeneratedClientDispatchResult ExecuteGeneratedRoutePlayerClientSync(const TSharedPtr<MClientConnection>& Client, const SGeneratedClientRouteRequest& Request, const TByteArray& Packet);
    EGeneratedClientDispatchResult ExecuteGeneratedRouteLoginRpcOrLegacy(const TSharedPtr<MClientConnection>& Client, const SGeneratedClientRouteRequest& Request, const TByteArray& Packet);
    EGeneratedClientDispatchResult ExecuteGeneratedRouteByPolicy(
        const TSharedPtr<MClientConnection>& Client,
        const MString& RouteKey,
        const MString& WrapMode,
        const SGeneratedClientRouteRequest* Request,
        const TByteArray& Packet);
    EGeneratedClientDispatchResult HandleGeneratedClientRoute(const SGeneratedClientRouteRequest& Request) override;
    void HandleClientPacket(uint64 ConnectionId, const TByteArray& Data);
    bool HandleClientFunctionCall(uint64 ConnectionId, const TByteArray& Data);
    void HandleLoginServerMessage(uint8 Type, const TByteArray& Data);
    void HandleWorldServerMessage(uint8 Type, const TByteArray& Data);
    void HandleRouterServerMessage(uint8 Type, const TByteArray& Data);

    // 使用分发器注册 / 处理具体消息
    void InitLoginMessageHandlers();
    void InitWorldMessageHandlers();
    void InitRouterMessageHandlers();

    void OnLogin_PlayerLogin(const SPlayerLoginResponseMessage& Message);
    void OnWorld_PlayerLogout(const SPlayerLogoutMessage& Message);
    void OnWorld_PlayerClientSync(const SPlayerClientSyncMessage& Message);
    void OnRouter_ServerRegisterAck(const SServerRegisterAckMessage& Message);
    void OnRouter_RouteResponse(const SRouteResponseMessage& Message);
    void SendRouterRegister();
    uint64 QueryRoute(EServerType ServerType, uint64 PlayerId = 0);
    void ApplyRoute(EServerType ServerType, uint32 ServerId, const MString& ServerName, const MString& Address, uint16 Port);
    void InvalidateResolvedRoute(EServerType ServerType, uint64 PlayerId);
    void InvalidateResolvedRoutesForPlayer(uint64 PlayerId);
    void RemovePendingResolvedClientRoutesForPlayer(uint64 PlayerId);
    void FlushPendingWorldLogins();
    void FlushPendingResolvedClientRoutes(EServerType ServerType);
    MString BuildDebugStatusJson() const;
};
