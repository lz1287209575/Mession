#pragma once

#include "Core/Net/NetCore.h"
#include "Core/Net/Socket.h"
#include "Core/Net/HttpDebugServer.h"
#include "Common/Logger.h"
#include "Common/NetServerBase.h"
#include "Common/ServerConnection.h"
#include "Common/ServerMessages.h"
#include "Servers/Gateway/GatewayRpcService.h"
#include "Servers/Login/LoginRpcService.h"
#include "Servers/World/WorldRpcService.h"
#include <random>
#include <thread>
#include <chrono>

// 登录服务器配置
struct SLoginConfig
{
    uint16 ListenPort = 8002;  // 网关连接端口
    FString RouterServerAddr = "127.0.0.1";
    uint16 RouterServerPort = 8005;
    uint32 SessionKeyMin = 100000;
    uint32 SessionKeyMax = 999999;
    uint16 DebugHttpPort = 0;       // 调试 HTTP 端口（0 = 关闭）
};

// 在线会话
struct SSession
{
    uint64 PlayerId;
    uint32 SessionKey;
    uint64 ConnectionId;
    uint64 ExpireTime;  // 过期时间戳
};

struct SGatewayPeer
{
    TSharedPtr<INetConnection> Connection;
    bool bAuthenticated = false;
    uint32 ServerId = 0;
    EServerType ServerType = EServerType::Unknown;
    FString ServerName;
};

// 登录服务器
MCLASS()
class MLoginServer : public MNetServerBase, public MReflectObject
{
public:
    MGENERATED_BODY(MLoginServer, MReflectObject, 0)

private:
    SLoginConfig Config;
    TMap<uint64, SGatewayPeer> GatewayConnections;
    TSharedPtr<MServerConnection> RouterServerConn;
    TMap<uint32, SSession> Sessions;
    TMap<uint64, uint32> PlayerSessions;
    std::mt19937 Rng;

    // 调试 HTTP 服务器
    TUniquePtr<MHttpDebugServer> DebugServer;

    // Login 级 RPC Service（处理跨服务器 RPC）
    MLoginService LoginService;

    // 服务器消息分发器
    MServerMessageDispatcher GatewayMessageDispatcher;
    MServerMessageDispatcher RouterMessageDispatcher;

public:
    MLoginServer();
    ~MLoginServer() { Shutdown(); }

    bool LoadConfig(const FString& ConfigPath);
    bool Init(int InPort = 0);
    void Tick();
    void Run() override { MNetServerBase::Run(); }

    uint16 GetListenPort() const override;
    void OnAccept(uint64 ConnId, TSharedPtr<INetConnection> Conn) override;
    void TickBackends() override;
    void ShutdownConnections() override;
    void OnRunStarted() override;

    uint32 CreateSession(uint64 PlayerId, uint64 ConnectionId);
    bool ValidateSession(uint32 SessionKey, uint64& OutPlayerId);
    void RemoveSession(uint32 SessionKey);

    MDECLARE_SERVER_HOSTED_RPC_METHOD("MLoginServer", Login, Rpc_OnRouterServerRegisterAck, (uint8 Result), NetServer, ServerToServer, true)

private:
    void HandleGatewayPacket(uint64 ConnectionId, const TArray& Data);
    bool SendServerMessage(uint64 ConnectionId, uint8 Type, const TArray& Payload);
    template<typename TMessage>
    bool SendServerMessage(uint64 ConnectionId, EServerMessageType Type, const TMessage& Message)
    {
        return SendServerMessage(ConnectionId, static_cast<uint8>(Type), BuildPayload(Message));
    }
    void HandleRouterServerMessage(uint8 Type, const TArray& Data);
    void SendRouterRegister();
    uint32 GenerateSessionKey();
    uint64 FindAuthenticatedPeerConnectionId(EServerType ServerType) const;
    FString BuildDebugStatusJson() const;

    // 分发器注册与具体处理函数
    void InitGatewayMessageHandlers();
    void InitRouterMessageHandlers();
    void OnGateway_ServerHandshake(uint64 ConnectionId, const TArray& Payload);
    void OnGateway_Heartbeat(uint64 ConnectionId, const SHeartbeatMessage& Message);
    void OnGateway_PlayerLogin(uint64 ConnectionId, const SPlayerLoginRequestMessage& Request);
    void OnGateway_SessionValidateRequest(uint64 ConnectionId, const SSessionValidateRequestMessage& Request);
    void OnRouter_ServerRegisterAck(const SServerRegisterAckMessage& Message);
};
