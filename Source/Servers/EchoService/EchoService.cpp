#include "Servers/EchoService/EchoService.h"
#include "Common/Net/Rpc/RpcServerCall.h"
#include "Servers/App/ServerCallAsyncSupport.h"
#include "Servers/App/ServiceContainer.h"
#include "Common/Runtime/Log/Logger.h"
#include "Common/Runtime/Async/MAsync.h"

bool MEchoService::LoadConfig(const MString& /*ConfigPath*/)
{
    return true;
}

bool MEchoService::Init(int InPort)
{
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

void MEchoService::Tick()
{
    MServiceContainer::Get().TickAll(0.0f);
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
    MServiceContainer::Get().ShutdownAll();

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

        // 1. 注册到 MServiceContainer（全局 transport map）
        MServiceContainer::Get().Register(PeerConn);

        // 2. 注册到本进程 MServerRuntimeContext（用于 RpcRuntimeContext::ResolveServerTransport）
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

    const SActorRoute Route = MActorRouter::Get().FindActor(Request.TargetActorId);

    // 响应中带回本进程的 (ServiceId, InstId) 让远端能反调
    const uint64 SelfActorId = MServiceId::Make(Config.LocalServerType, Config.LocalInstId);

    FSampleEchoResponse Response;
    Response.Echo = Request.Message + " [echoed]";
    Response.SourceActorId = SelfActorId;        // 远端用此反查
    Response.SourceServerName = Config.ServiceName;

    // 本机 Actor：直接返
    if (Route.ActorId != 0 && Route.ServerType == EServerType::Unknown)
    {
        return MServerCallAsyncSupport::MakeSuccessFuture(std::move(Response));
    }

    // 远程 Actor：跨进程转发（链 2 路径——ServiceA → ServiceB 直连，不走 Gateway）
    if (Route.ActorId != 0)
    {
        // 通过 MServiceContainer 查 peer transport
        TSharedPtr<MServerConnection> TargetConn = MServiceContainer::Get().Resolve(TargetServiceType);
        if (!TargetConn || !TargetConn->IsConnected())
        {
            return MServerCallAsyncSupport::MakeErrorFuture<FSampleEchoResponse>(
                "peer_transport_unavailable", "Echo");
        }

        // 直接转发到 peer——Service B 处理完会回 A，A 再回原始调用方（Gateway）。
        // 这里调用方可能是 Gateway（ClientCall 入口）或另一 Service（链 2 内部转发）。
        return CallServerFunction<FSampleEchoResponse>(
            TargetConn, "MEchoService", "Echo", Request);
    }

    return MServerCallAsyncSupport::MakeErrorFuture<FSampleEchoResponse>(
        "actor_not_found", "Echo");
}