#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/IO/Socket/Socket.h"
#include "Common/Net/NetServerBase.h"
#include "Common/Net/ServerConnection.h"
#include "Common/Net/Rpc/RpcDispatch.h"
#include "Common/Runtime/Log/Log.h"
#include "Common/Runtime/Object/Result.h"
#include "Protocol/Messages/Common/AppMessages.h"
#include "Protocol/Messages/Common/ClientDownlinkMessages.h"
#include "Protocol/Messages/Common/ControlPlaneMessages.h"
#include "Protocol/Messages/EchoService/FSampleEchoMessages.h"
#include "Common/Runtime/Async/MAsync.h"
#include "Servers/App/ServerCallAsyncSupport.h"
#include "Servers/App/ServiceMain.h"

class MGatewayServer;
extern MGatewayServer* GGlobalGateway;

MSTRUCT()
struct SGatewayConfig
{
    MPROPERTY(Meta=(Cli="--listen"))
    uint16 ListenPort = 8001;

    // Service 注册中心地址（"127.0.0.1:18000"）。PoC 阶段强制要求传
    // ——MEndpointCache 启动期就要连 Registry 取全量 endpoint。
    MPROPERTY(Meta=(Cli="--registry"))
    MString RegistryAddr;

    // PoC 阶段：Gateway 没有 LocalActorIds / LocalInstId（不做业务），
    // 但 MakeLocalEndpoint 模板需要这些字段——这里给个空的默认值让
    // 模板实例化能通过。运行时 Gateway 通过 MEndpointCache 寻址 EchoService。
    MPROPERTY()
    EServerType LocalServerType = EServerType::Unknown;

    MPROPERTY()
    uint32 LocalServerId = 0;

    MPROPERTY()
    TVector<uint32> LocalActorIds;

    // 日志：ServiceMain::Run 会读这些字段初始化 MLog 管道。
    MPROPERTY(Meta=(Cli="--log-file"))
    MString LogFilePath = "";

    MPROPERTY(Meta=(Cli="--log-rotate-bytes"))
    uint64 LogRotateBytes = 100ull * 1024ull * 1024ull;

    MPROPERTY(Meta=(Cli="--log-archives"))
    uint32 LogArchives = 5;

    MPROPERTY(Meta=(Cli="--log-config"))
    MString LogConfigPath = "";
};

MCLASS(Type=Server)
class MGatewayServer : public MNetServerBase, public MObject
{
public:
    MGENERATED_BODY(MGatewayServer, MObject, 0)
public:
    using MObject::Tick;

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

    static MGatewayServer* GetSingleton() { return GGlobalGateway; }

private:
    void HandleClientPacket(uint64 ConnectionId, const TByteArray& Data);

    SGatewayConfig Config;
    TMap<uint64, TSharedPtr<INetConnection>> ClientConnections;
};
