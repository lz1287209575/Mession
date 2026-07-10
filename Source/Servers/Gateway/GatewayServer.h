#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/IO/Socket/Socket.h"
#include "Common/Net/NetServerBase.h"
#include "Common/Net/ServerConnection.h"
#include "Common/Net/Rpc/RpcRuntimeContext.h"
#include "Common/Net/Rpc/RpcDispatch.h"
#include "Common/Runtime/Log/Logger.h"
#include "Common/Runtime/Object/Result.h"
#include "Protocol/Messages/Common/AppMessages.h"
#include "Protocol/Messages/Common/ClientDownlinkMessages.h"
#include "Protocol/Messages/Common/ControlPlaneMessages.h"
#include "Protocol/Messages/Common/ForwardedClientCallMessages.h"
#include "Protocol/Messages/EchoService/FSampleEchoMessages.h"
#include "Common/Runtime/Async/MAsync.h"
#include "Servers/App/ServerCallAsyncSupport.h"
#include "Servers/App/ServiceMain.h"

class MGatewayServer;
extern MGatewayServer* GGlobalGateway;

struct SGatewayConfig
{
    uint16 ListenPort = 8001;
    TVector<MServiceMain::SServicePeerConfig> Peers;  // 启动时主动连接的 peer（PoC 阶段：EchoService）
};

MCLASS(Type=Server)
class MGatewayServer : public MNetServerBase, public MObject, public MServerRuntimeContext
{
public:
    MGENERATED_BODY(MGatewayServer, MObject, 0)
public:
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

    MFUNCTION(ServerCall)
    MFuture<TResult<SEmptyServerMessage, FAppError>> PushClientDownlink(const FClientDownlinkPushRequest& Request);

    // 传输层握手 / 心跳桩——EchoService 对端 Rpc_OnServerHandshake / Rpc_OnHeartbeat 同源。
    MFUNCTION(ServerCall)
    MFuture<TResult<SEmptyServerMessage, FAppError>> Rpc_OnServerHandshake(uint32 DummyServerId);

    MFUNCTION(ServerCall)
    MFuture<TResult<SEmptyServerMessage, FAppError>> Rpc_OnHeartbeat(uint32 Seq);

    void ApplyConfig(const SGatewayConfig& InConfig) { Config = InConfig; }

    /**
     * BuildConfig — GatewayServer 入口参数解析。
     * 公共参数（--port / --peers / --listen）由 MServiceMain::ParseCommonArgs 处理。
     */
    static SGatewayConfig BuildConfig(int argc, char** argv);

    static MGatewayServer* GetSingleton() { return GGlobalGateway; }

private:
    void HandleClientPacket(uint64 ConnectionId, const TByteArray& Data);
    void HandleBackendPacket(
        const TSharedPtr<MServerConnection>& Connection,
        uint8 PacketType,
        const TByteArray& Data,
        const char* PeerName);

    void ConnectAllPeers();

    SGatewayConfig Config;
    TMap<uint64, TSharedPtr<INetConnection>> ClientConnections;
    MServerConnectionManager BackendConnectionManager;
};
