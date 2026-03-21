#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/IO/Socket/Socket.h"
#include "Common/Net/HttpDebugServer.h"
#include "Common/Runtime/Log/Logger.h"
#include "Common/Net/NetServerBase.h"
#include "Common/Net/ServerConnection.h"
#include "Protocol/ServerMessages.h"
#include <random>
#include <thread>
#include <chrono>

// 登录服务器配置
struct SLoginConfig
{
    uint16 ListenPort = 8002;  // 网关连接端口
    MString RouterServerAddr = "127.0.0.1";
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
    MString ServerName;
};

// 登录服务器
MCLASS()
class MLoginServer : public MNetServerBase, public MObject
{
public:
    MGENERATED_BODY(MLoginServer, MObject, 0)

private:
    SLoginConfig Config;
    TMap<uint64, SGatewayPeer> GatewayConnections;
    TSharedPtr<MServerConnection> RouterServerConn;
    TMap<uint32, SSession> Sessions;
    TMap<uint64, uint32> PlayerSessions;
    std::mt19937 Rng;

    // 调试 HTTP 服务器
    TUniquePtr<MHttpDebugServer> DebugServer;

public:
    MLoginServer();
    ~MLoginServer() { Shutdown(); }
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
    MFUNCTION(NetServer, Rpc=ServerToServer, Reliable=true, Endpoint=Login)
    void Rpc_OnServerHandshake(uint32 ServerId, uint8 ServerTypeValue, const MString& ServerName);
    MFUNCTION(NetServer, Rpc=ServerToServer, Reliable=true, Endpoint=Login)
    void Rpc_OnHeartbeat(uint32 Sequence);

    uint32 CreateSession(uint64 PlayerId, uint64 ConnectionId);
    bool ValidateSession(uint32 SessionKey, uint64& OutPlayerId);
    void RemoveSession(uint32 SessionKey);
    MFUNCTION(NetServer, Rpc=ServerToServer, Reliable=true, Endpoint=Login)
    void Rpc_OnPlayerLoginRequest(uint64 ClientConnectionId, uint64 PlayerId);
    MFUNCTION(NetServer, Rpc=ServerToServer, Reliable=true, Endpoint=Login)
    void Rpc_OnSessionValidateRequest(uint64 ValidationRequestId, uint64 PlayerId, uint32 SessionKey);
    MFUNCTION(NetServer, Rpc=ServerToServer, Reliable=true, Endpoint=Login)
    void Rpc_OnRouterServerRegisterAck(uint8 Result);

private:
    void HandleGatewayPacket(uint64 ConnectionId, const TByteArray& Data);
    bool SendServerPacket(uint64 ConnectionId, uint8 PacketType, const TByteArray& Payload);
    template<typename TMessage>
    bool SendServerPacket(uint64 ConnectionId, EServerMessageType PacketType, const TMessage& Message)
    {
        return SendServerPacket(ConnectionId, static_cast<uint8>(PacketType), BuildPayload(Message));
    }
    void HandleRouterServerPacket(uint8 PacketType, const TByteArray& Data);
    void SendRouterRegister();
    uint32 GenerateSessionKey();
    uint64 FindAuthenticatedPeerConnectionId(EServerType ServerType) const;
    MString BuildDebugStatusJson() const;

    void OnGateway_SessionValidateRequest(uint64 ConnectionId, const SSessionValidateRequestMessage& Request);
    void OnRouter_ServerRegisterAck(const SNodeRegisterAckMessage& Message);
};
