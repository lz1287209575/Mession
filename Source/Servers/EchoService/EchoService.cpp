#include "Servers/EchoService/EchoService.h"
#include "Common/Net/Rpc/MRpcChannel.h"
#include "Common/Net/Rpc/RpcServerCall.h"
#include "Common/Net/Rpc/RpcTransport.h"
#include "Servers/App/ServerCallAsyncSupport.h"
#include "Common/Runtime/Log/Logger.h"
#include "Common/Runtime/Async/MAsync.h"
#include "Common/Runtime/Id.h"

bool MEchoService::Init(int InPort)
{
    // MService<SEchoServiceConfig>::LoadConfig(argc, argv) 已经在 main
    // 入口（ServiceMain.h::Run 模板）跑过，这里直接从单例拷一份即可。
    Config = MService<SEchoServiceConfig>::GetConfig();

    if (InPort > 0)
    {
        Config.ListenPort = static_cast<uint16>(InPort);
    }

    if (Config.ListenPort == 0)
    {
        LOG_ERROR("EchoService: ListenPort is 0");
        return false;
    }

    bRunning = true;
    MLogger::LogStartupBanner(Config.ServiceName.c_str(), Config.ListenPort, 0);

    // 标记本进程本地 Server 信息——MServerConnection 响应包分发时依赖 LocalInfo。
    MServerConnection::SetLocalInfo(Config.LocalServerId, Config.LocalServerType, Config.ServiceName.c_str());

    RegisterLocalActors();
    ConnectAllPeers();

    return true;
}

void MEchoService::TickBackends()
{
    // 后端 tick：MNetServerBase::Run 主循环每帧调一次，把 RpcTransports 里的
    // 对端连接推心跳 / 收包。
    for (const auto& Pair : GetRpcTransports())
    {
        if (Pair.second)
        {
            Pair.second->Tick(0.0f);
        }
    }
}

uint16 MEchoService::GetListenPort() const
{
    return Config.ListenPort;
}

void MEchoService::OnAccept(uint64 /*ConnId*/, TSharedPtr<INetConnection> /*Conn*/)
{
    // PoC 阶段不接受 peer 连接（EchoService 不监听 client TCP——只有 Gateway 监听）
}

void MEchoService::ShutdownConnections()
{
    for (uint32 InstId : Config.LocalActorIds)
    {
        const uint64 ActorId = MServiceId::Make(Config.LocalServerType, InstId);
        MActorRouter::Get().UnregisterActor(ActorId);
    }
    ActorMessages.clear();

    ClearRpcTransports();
}

void MEchoService::OnRunStarted()
{
    LOG_INFO("%s running on port %u",
             Config.ServiceName.c_str(),
             static_cast<unsigned>(Config.ListenPort));
}

void MEchoService::RegisterLocalActors()
{
    for (uint32 InstId : Config.LocalActorIds)
    {
        // 本机 Actor 标记 EServerType::Unknown——MActorRouter::SendToActor 在
        // ServerType == Unknown 时走 IsActorLocal 分支（见 ActorRouter.cpp:42-46）。
        const uint64 ActorId = MServiceId::Make(Config.LocalServerType, InstId);
        MActorRouter::Get().RegisterActor(ActorId, EServerType::Unknown);
        ActorMessages[ActorId] = MString();
        LOG_INFO("%s: registered local actor ServiceId=%u InstId=%u (ActorId=%llu)",
                 Config.ServiceName.c_str(),
                 static_cast<unsigned>(MServiceId::GetServiceId(ActorId)),
                 static_cast<unsigned>(MServiceId::GetInstId(ActorId)),
                 static_cast<unsigned long long>(ActorId));
    }
}

void MEchoService::ConnectAllPeers()
{
    for (const SServicePeerConfig& Peer : Config.Peers)
    {
        const uint32 PeerServerId = MUniqueIdGenerator::Generate();
        const SServerConnectionConfig PeerConfig(
            PeerServerId,
            Peer.ServerType,
            GetServerTypeDisplayName(Peer.ServerType),
            Peer.Address,
            Peer.Port);

        TSharedPtr<MServerConnection> PeerConn = MakeShared<MServerConnection>(PeerConfig);
        PeerConn->Connect();

        // 注册到本进程 MServerRuntimeContext（用作 RpcRuntimeContext::ResolveServerTransport 与 MActorRouter 跨进程寻址）
        RegisterRpcTransport(Peer.ServerType, PeerConn);

        LOG_INFO("%s: connected to peer %s at %s:%u",
                 Config.ServiceName.c_str(),
                 GetServerTypeDisplayName(Peer.ServerType),
                 Peer.Address.c_str(),
                 static_cast<unsigned>(Peer.Port));
    }
}

MFUTURE(FSampleEchoResponse) MEchoService::Echo(const FSampleEchoRequest& Request)
{
    if (Request.TargetActorId == 0)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FSampleEchoResponse>(
            "actor_id_required", "Echo");
    }

    // 从 TargetActorId 拆解目标 Service 类型
    const EServerType TargetServiceType = MServiceId::GetServiceType(Request.TargetActorId);
    const uint32 TargetInstId = MServiceId::GetInstId(Request.TargetActorId);

    LOG_INFO("%s: Echo received TargetServiceType=%s TargetInstId=%u",
             Config.ServiceName.c_str(),
             GetServerTypeDisplayName(TargetServiceType),
             static_cast<unsigned>(TargetInstId));

    // 响应中带回本进程的 (ServiceId, InstId) 让远端能反调
    const uint64 SelfActorId = MServiceId::Make(Config.LocalServerType, Config.LocalInstId);

    FSampleEchoResponse Response;
    Response.Echo = Request.Message + " [echoed]";
    Response.SourceActorId = SelfActorId;        // 远端用此反查
    Response.SourceServerName = Config.ServiceName;

    // 本机 Actor：MActorRouter 命中本进程 → 直接返（避免走跨进程）
    if (MActorRouter::Get().IsActorLocal(Request.TargetActorId))
    {
        return MServerCallAsyncSupport::MakeSuccessFuture(std::move(Response));
    }

    // 跨进程转发（链 2 路径：ServiceA → ServiceB 走 MRpcChannel::CallToActor）
    // MActorRouter 已经在对端连接建立时通过 MActorRouter::UpdateActorRoute 注册了对端 ActorIds；
    // 这里直接用 ActorId 寻址，不直接拿 connection。
    return MRpcChannel::Get().CallToActor<FSampleEchoResponse>(
        this,
        Request.TargetActorId,
        "MEchoService",
        "Echo",
        Request);
}

// CreateService 工厂——MServiceMain::Run 模板按 C++ name mangling 调用。
// MObject 生命周期由 TSharedPtr 体系管理；ServiceMain.h 中 namespace
// 内的 CreateService<TService>() 模板已经用 NewMObject<TService>() 兜底。
// 这里不再单独导出 extern "C" 工厂——Service 自身的类型 + ServiceMain
// 的 NewMObject 路径已经够用。
//
// 如果未来某 Service 想要更精细的初始化（例如注入外部依赖），可以在
// 此文件加一个特殊的 extern "C" TSharedPtr<MObject> CreateMEchoService()
// 工厂，并相应去掉 ServiceMain.h::CreateService 的兜底调用——目前不需要。