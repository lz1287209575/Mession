#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/IO/Socket/Socket.h"
#include "Common/Net/HttpDebugServer.h"
#include "Common/Runtime/Log/Logger.h"
#include "Common/Net/NetServerBase.h"
#include "Common/Net/ServerConnection.h"
#include "Protocol/ServerMessages.h"
#include "Common/Runtime/Reflect/Reflection.h"
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
class MRouterServer : public MNetServerBase, public MObject
{
public:
    MGENERATED_BODY(MRouterServer, MObject, 0)

private:
    SRouterConfig Config;
    TMap<uint64, SRouterPeer> Peers;
    TMap<uint64, SPlayerRouteBinding> PlayerRouteBindings;
    uint64 TickCounter = 0;

    // 调试 HTTP 服务器
    TUniquePtr<MHttpDebugServer> DebugServer;

public:
    MRouterServer() = default;
    ~MRouterServer() { Shutdown(); }
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
    MFUNCTION(NetServer, Rpc=ServerToServer, Reliable=true, Endpoint=Router)
    void Rpc_OnServerHandshake(uint32 ServerId, uint8 ServerTypeValue, const MString& ServerName);
    MFUNCTION(NetServer, Rpc=ServerToServer, Reliable=true, Endpoint=Router)
    void Rpc_OnHeartbeat(uint32 Sequence);
    MFUNCTION(NetServer, Rpc=ServerToServer, Reliable=true, Endpoint=Router)
    void Rpc_OnPeerServerRegister(uint32 ServerId, uint8 ServerTypeValue, const MString& ServerName, const MString& Address, uint16 Port, uint16 ZoneId);
    MFUNCTION(NetServer, Rpc=ServerToServer, Reliable=true, Endpoint=Router)
    void Rpc_OnPeerServerLoadReport(uint32 ServerId, uint32 CurrentLoad, uint32 Capacity);
    MFUNCTION(NetServer, Rpc=ServerToServer, Reliable=true, Endpoint=Router)
    void Rpc_OnPeerRouteQuery(uint32 ServerId, uint64 RequestId, uint8 RequestedTypeValue, uint64 PlayerId, uint16 ZoneId);

private:
    void HandlePacket(uint64 ConnectionId, const TByteArray& Data);
    bool SendServerPacket(uint64 ConnectionId, uint8 PacketType, const TByteArray& Payload);
    template<typename TMessage>
    bool SendServerPacket(uint64 ConnectionId, EServerMessageType PacketType, const TMessage& Message)
    {
        return SendServerPacket(ConnectionId, static_cast<uint8>(PacketType), BuildPayload(Message));
    }
    const SRouterPeer* SelectRouteTarget(EServerType RequestedType, uint64 PlayerId, uint16 ZoneId = 0);
    const SRouterPeer* FindRegisteredServerById(uint32 ServerId) const;
    void RemovePeer(uint64 ConnectionId);
    MString BuildDebugStatusJson() const;
};
