#include "Servers/EchoService/EchoService.h"
#include "Common/Net/Rpc/MRpcChannel.h"
#include "Common/Net/Rpc/RpcServerCall.h"
#include "Common/Net/Rpc/RpcTransport.h"
#include "Common/Net/ServiceDiscovery/Endpoint.h"
#include "Common/Net/ServiceDiscovery/EndpointCache.h"
#include "Servers/App/ServerCallAsyncSupport.h"
#include "Common/Runtime/Log/Log.h"
#include "Common/Runtime/Async/MAsync.h"
#include "Common/Runtime/Id.h"

bool MEchoService::Init(int InPort)
{
    // MService<SEchoServiceConfig>::LoadConfig(argc, argv) 已经在 main
    // 入口（ServiceMain.h::Run 模板）跑过，这里直接从单例拷一份即可。
    Config = MService<SEchoServiceConfig>::GetConfig();

    // 后处理派生：LocalServerTypeName 是 CLI 友好的字符串字段；本地业务代码
    // 期望直接拿到 EServerType。如果用户没传 --local-type，保持 MSTRUCT
    // 默认值 ("Echo")，下面再把 LocalServerType 派生过来。
    const EServerType Derived = MServiceMain::ParseLocalServerType(Config.LocalServerTypeName);
    if (Derived == EServerType::Unknown && !Config.LocalServerTypeName.empty())
    {
        LOG_WARN("MEchoService: --local-type=%s did not match any known server type; "
                 "leaving LocalServerType=Unknown",
                 Config.LocalServerTypeName.c_str());
    }
    Config.LocalServerType = Derived;

    if (InPort > 0)
    {
        Config.ListenPort = static_cast<uint16>(InPort);
    }

    if (Config.ListenPort == 0)
    {
        LOG_ERROR("EchoService: ListenPort is 0");
        return false;
    }

    if (Config.RegistryAddr.empty())
    {
        LOG_ERROR("MEchoService: --registry=<host:port> is required");
        return false;
    }

    // LocalServerId 没传 → MUniqueIdGenerator::Generate 自给；截到 uint32 避免溢出。
    if (Config.LocalServerId == 0)
    {
        Config.LocalServerId = static_cast<uint32>(MUniqueIdGenerator::Generate());
    }

    bRunning = true;
    CORE_LOG(Info, ":: Mession Game Server :: (v%s)", "1.0.0");
    NET_LOG(Info, "Starting %s on port %u", Config.ServiceName.c_str(),
        static_cast<unsigned>(Config.ListenPort));

    // 标记本进程本地 Server 信息——MServerConnection 响应包分发时依赖 LocalInfo。
    MServerConnection::SetLocalInfo(Config.LocalServerId, Config.LocalServerType, Config.ServiceName.c_str());

    // 注册到 Registry：
    // 1. AttachEventLoop：把 Service 进程内的 NetEventLoop 交给 Cache
    // 2. BindRegistry：拆 host:port 触发首次 TCP 连接
    // 3. RegisterLocal：发 Register packet
    MEndpointCache::Get().AttachEventLoop(&EventLoop);
    MString RegHost;
    uint16 RegPort = 0;
    if (!ParseAddrPort(Config.RegistryAddr, RegHost, RegPort))
    {
        LOG_ERROR("MEchoService: malformed --registry=%s (expected host:port)", Config.RegistryAddr.c_str());
        return false;
    }
    MEndpointCache::Get().BindRegistry(RegHost, RegPort);
    MEndpointCache::Get().RegisterLocal(MakeLocalEndpoint(Config));

    RegisterLocalActors();

    return true;
}

void MEchoService::TickBackends()
{
    // 后端 tick：MNetServerBase::Run 主循环每帧调一次，把 MEndpointCache
    // 内部的 Registry 心跳发送 + 断线重连维护起来。
    MEndpointCache::Get().Tick(0.0f);
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

    // 注销 Registry 连接 + 清空 ConnectionPool
    MEndpointCache::Get().DeregisterAndShutdown();
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
    // MEndpointCache::GetOrConnect 走 Registry 推送的 endpoint 列表 + lazy connect。
    return MRpcChannel::Get().CallToActor<FSampleEchoResponse>(
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
//
// CreateService 工厂约定（Service 自定义工厂方法约定）：
//   1. 默认路径：MServiceMain::CreateService<TService>() 模板自动调
//      NewMObject<TService>(nullptr, typeid(TService).name())——零配置，
//      适用于所有不依赖外部注入的 Service。
//   2. 覆盖路径：在 Service 的 cpp 内提供 extern "C" 重载：
//          extern "C" TSharedPtr<MObject> CreateMEchoService()
//          { return NewMObject<MEchoService>(nullptr, "MEchoService"); }
//      然后 MServiceMain::Run<TService, TConfig> 改成先查强符号、再
//      回退到 CreateService<TService>() 模板。
//   3. 命名要求：函数名必须严格等于 "Create" + Service 类名（无名字
//      mangling 干扰，dlopen/dlsym 也能拿到同一符号）。当前 Service
//      都不需要第 2 步，但保留扩展空间。