#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/IO/Socket/Socket.h"
#include "Common/Net/NetServerBase.h"
#include "Common/Net/Rpc/RpcRuntimeContext.h"
#include "Common/Net/Routing/ActorRouter.h"
#include "Common/Runtime/Async/MAsync.h"
#include "Common/Runtime/Object/Object.h"
#include "Common/Runtime/Object/Result.h"
#include "Common/Runtime/Log/Logger.h"
#include "Common/Runtime/Id.h"
#include "Servers/App/ServiceId.h"
#include "Protocol/Messages/EchoService/FSampleEchoMessages.h"

// 进程间连接 peer 配置。PoC 阶段由 --peers 命令行参数解析；每个 peer 一项。
struct SServicePeerConfig
{
    EServerType ServerType = EServerType::Unknown;
    MString Address = "127.0.0.1";
    uint16 Port = 0;
};

struct SEchoServiceConfig
{
    uint16 ListenPort = 0;
    MString ServiceName = "MEchoService";
    EServerType LocalServerType = EServerType::Unknown;
    uint32 LocalServerId = 0;
    uint32 LocalInstId = 0;  // 本进程在 LocalServerType 类型下的实例号
    TVector<uint32> LocalActorIds;  // 本进程持有的其他 Actor 的 InstId 列表；最终 ActorId = MServiceId::Make(LocalServerType, InstId)
    TVector<SServicePeerConfig> Peers;  // 启动时连接的 peer 列表（Gateway + 其它 EchoService）
};

MCLASS(Type=Service)
class MEchoService : public MNetServerBase, public MObject, public MServerRuntimeContext
{
public:
    MGENERATED_BODY(MEchoService, MObject, 0)
public:
    using MObject::Tick;

    bool LoadConfig(const MString& ConfigPath);
    bool Init(int InPort = 0);
    void Tick();
    void Run() override { MNetServerBase::Run(); }

    uint16 GetListenPort() const override;
    void OnAccept(uint64 ConnId, TSharedPtr<INetConnection> Conn) override;
    void ShutdownConnections() override;
    void OnRunStarted() override;

    void ApplyConfig(const SEchoServiceConfig& InConfig) { Config = InConfig; }

    // EchoService 暴露的 ServerCall——Gateway 通过 ClientFunctionRoute 路由表查到本类后调用。
    MFUNCTION(ServerCall)
    MFUTURE(FSampleEchoResponse) Echo(const FSampleEchoRequest& Request);

private:
    // 建立到所有 peer 的连接 + 注册到 MServiceContainer + 注册到本进程 MServerRuntimeContext。
    void ConnectAllPeers();

    // 本机 Actor 注册到 MActorRouter（ServerType=Unknown 表示本机）。
    void RegisterLocalActors();

    SEchoServiceConfig Config;
    TMap<uint64, MString> ActorMessages;
};