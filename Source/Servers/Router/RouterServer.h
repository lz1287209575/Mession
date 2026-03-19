#pragma once

#include "Common/MLib.h"
#include "Core/Net/Socket.h"
#include "Core/Net/HttpDebugServer.h"
#include "Common/Logger.h"
#include "Common/NetServerBase.h"
#include "Common/ServerConnection.h"
#include "Common/ServerMessages.h"
#include "NetDriver/Reflection.h"
#include <thread>
#include <chrono>

struct SRouterConfig
{
    uint16 ListenPort = 8005;
    uint32 RouteLeaseSeconds = 300;
    uint16 DebugHttpPort = 0;      // 调试 HTTP 端口（0 = 关闭）
};

struct SRouterPeer
{
    TSharedPtr<INetConnection> Connection;
    bool bAuthenticated = false;
    bool bRegistered = false;
    uint32 ServerId = 0;
    EServerType ServerType = EServerType::Unknown;
    MString ServerName;
    MString Address = "127.0.0.1";
    uint16 Port = 0;
    uint16 ZoneId = 0;
    uint32 CurrentLoad = 0;
    uint32 Capacity = 0;
};

struct SPlayerRouteBinding
{
    uint64 PlayerId = 0;
    uint32 WorldServerId = 0;
    uint64 LeaseExpireTick = 0;
};

MCLASS()
class MRouterServer : public MNetServerBase
{
private:
    SRouterConfig Config;
    TMap<uint64, SRouterPeer> Peers;
    TMap<uint64, SPlayerRouteBinding> PlayerRouteBindings;
    uint64 TickCounter = 0;

    // 调试 HTTP 服务器
    TUniquePtr<MHttpDebugServer> DebugServer;

    MServerMessageDispatcher PeerMessageDispatcher;

public:
    MRouterServer() = default;
    ~MRouterServer() { Shutdown(); }

    bool LoadConfig(const MString& ConfigPath);
    bool Init(int InPort = 0);
    void Tick();
    void Run() override { MNetServerBase::Run(); }

    uint16 GetListenPort() const override;
    void OnAccept(uint64 ConnId, TSharedPtr<INetConnection> Conn) override;
    void TickBackends() override;
    void ShutdownConnections() override;
    void OnRunStarted() override;

private:
    void HandlePacket(uint64 ConnectionId, const TByteArray& Data);
    bool SendServerMessage(uint64 ConnectionId, uint8 Type, const TByteArray& Payload);
    template<typename TMessage>
    bool SendServerMessage(uint64 ConnectionId, EServerMessageType Type, const TMessage& Message)
    {
        return SendServerMessage(ConnectionId, static_cast<uint8>(Type), BuildPayload(Message));
    }
    const SRouterPeer* SelectRouteTarget(EServerType RequestedType, uint64 PlayerId, uint16 ZoneId = 0);
    const SRouterPeer* FindRegisteredServerById(uint32 ServerId) const;
    void RemovePeer(uint64 ConnectionId);
    MString BuildDebugStatusJson() const;
    void InitPeerMessageHandlers();
    void OnPeer_ServerHandshake(uint64 ConnectionId, const SServerHandshakeMessage& Message);
    void OnPeer_Heartbeat(uint64 ConnectionId, const SHeartbeatMessage& Message);
    void OnPeer_ServerRegister(uint64 ConnectionId, const SServerRegisterMessage& Message);
    void OnPeer_ServerLoadReport(uint64 ConnectionId, const SServerLoadReportMessage& Message);
    void OnPeer_RouteQuery(uint64 ConnectionId, const SRouteQueryMessage& Query);
};
