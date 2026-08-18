#include "Servers/EchoService/EchoService.h"
#include "Common/Net/Rpc/MRpcChannel.h"
#include "Common/Net/Rpc/RpcServerCall.h"
#include "Common/Net/Rpc/RpcTransport.h"
#include "Common/Net/ServiceDiscovery/Endpoint.h"
#include "Common/Net/ServiceDiscovery/EndpointCache.h"
#include "Common/Runtime/Actor/FActorMessage.h"
#include "Common/Runtime/Actor/MActorSystem.h"
#include "Common/Runtime/Async/MAsync.h"
#include "Common/Runtime/Id.h"
#include "Common/Runtime/Log/Log.h"
#include "Servers/App/ServerCallAsyncSupport.h"
#include "Servers/EchoService/MRankListActor.h"

#include <algorithm>
#include <thread>

bool MEchoService::Init(int InPort) {
    // MService<SEchoServiceConfig>::LoadConfig(argc, argv) 已经在 main
    // 入口（ServiceMain.h::Run 模板）跑过，这里直接从单例拷一份即可。
    Config = MService<SEchoServiceConfig>::GetConfig();

    // 后处理派生：LocalServerTypeName 是 CLI 友好的字符串字段；本地业务代码
    // 期望直接拿到 EServerType。如果用户没传 --local-type，保持 MSTRUCT
    // 默认值 ("Echo")，下面再把 LocalServerType 派生过来。
    const EServerType Derived = MServiceMain::ParseLocalServerType(Config.LocalServerTypeName);
    if (Derived == EServerType::Unknown && !Config.LocalServerTypeName.empty()) {
        LOG_WARN("MEchoService: --local-type=%s did not match any known server type; "
                 "leaving LocalServerType=Unknown",
                 Config.LocalServerTypeName.c_str());
    }
    Config.LocalServerType = Derived;

    if (InPort > 0) {
        Config.ListenPort = static_cast<uint16>(InPort);
    }

    if (Config.ListenPort == 0) {
        LOG_ERROR("EchoService: ListenPort is 0");
        return false;
    }

    if (Config.RegistryAddr.empty()) {
        LOG_ERROR("MEchoService: --registry=<host:port> is required");
        return false;
    }

    // LocalServerId 没传 → MUniqueIdGenerator::Generate 自给；截到 uint32 避免溢出。
    if (Config.LocalServerId == 0) {
        Config.LocalServerId = static_cast<uint32>(MUniqueIdGenerator::Generate());
    }

    bRunning           = true;
    GGlobalEchoService = this; // P5: EchoAwait codegen body 用全局单例(不能用 this,Frame 上下文)

    CORE_LOG(Info, ":: Mession Game Server :: (v%s)", "1.0.0");
    NET_LOG(Info, "Starting %s on port %u", Config.ServiceName.c_str(), static_cast<unsigned>(Config.ListenPort));

    // 标记本进程本地 Server 信息——MServerConnection 响应包分发时依赖 LocalInfo。
    MServerConnection::SetLocalInfo(Config.LocalServerId, Config.LocalServerType, Config.ServiceName.c_str());

    // P5: 多 Reactor 装配 —— 按 CPU 核数启动 Sub reactor。
    // N=0(单核机器)走单 Reactor 路径(等价旧行为);N>0 时网络 poll
    // 分布到各 Sub 线程,OnAccept 派发由 MNetServerBase::DispatchConnection 处理。
    const uint32 HardwareThreads = std::thread::hardware_concurrency();
    if (HardwareThreads > 1) {
        const uint32 SubCount = std::max(uint32(2), HardwareThreads);
        InitSubPool(SubCount);
        LOG_INFO("MEchoService: InitSubPool(%u) enabled (multi-reactor)", static_cast<unsigned>(SubCount));

        // 阶段 5:per-Sub 反射上下文注入 —— 每个 Sub 的入站 ServerCall
        // dispatch 需要各自的 ServiceInstance。只注全局(this)在 N>0 时
        // Sub 线程拿不到,导致 actor_route_invalid(见 B.7 验证)。
        for (uint32 SubId = 0; SubId < SubCount; ++SubId) {
            MEndpointCache::Get().SetServiceInstance(SubId, this);
        }

        // 阶段 5:actor 运行时绑定 SubPool(业务 actor 分布到各 Sub)
        MActorSystem::Get().Init(GetSubPool());

        // 阶段 5.7:注册第一个业务 actor —— 排行榜。
        // 所有权转给 MActorSystem(Unregister/Shutdown 时 delete)。
        MActorSystem::Get().Register(NewMObject<MRankListActor>(nullptr, "RankList"));
    }

    // 注册到 Registry：
    // 1. AttachEventLoop：把 Service 进程内的 NetEventLoop 交给 Cache
    //    —— Registry 连接始终挂基类 EventLoop(主线程 poll),即使多 Reactor
    //    模式也保持这样;跨线程直接调 Sub Loop 的 RegisterConnection 会与
    //    Sub 线程 poll 竞争 Connections map(此前 free(): invalid pointer)。
    // 2. BindRegistry：拆 host:port 触发首次 TCP 连接
    // 3. RegisterLocal：发 Register packet
    MEndpointCache::Get().AttachEventLoop(&EventLoop);
    MEndpointCache::Get().SetServiceInstance(this); // P2: install business dispatch target
    MString RegHost;
    uint16  RegPort = 0;
    if (!ParseAddrPort(Config.RegistryAddr, RegHost, RegPort)) {
        LOG_ERROR("MEchoService: malformed --registry=%s (expected host:port)", Config.RegistryAddr.c_str());
        return false;
    }
    MEndpointCache::Get().BindRegistry(RegHost, RegPort);
    MEndpointCache::Get().RegisterLocal(MakeLocalEndpoint(Config));

    RegisterLocalActors();

    return true;
}

void MEchoService::TickBackends() {
    // 后端 tick：MNetServerBase::Run 主循环每帧调一次，把 MEndpointCache
    // 内部的 Registry 心跳发送 + 断线重连维护起来。
    // 注意：DeltaTime 必须传真实帧间隔（与 Gateway 一致用 0.1f）——传 0.0f
    // 会让 HeartbeatTimer 永远不累积，心跳永不发送，Registry 超时 evict 本服务。
    MEndpointCache::Get().Tick(0.1f);
}

uint16 MEchoService::GetListenPort() const {
    return Config.ListenPort;
}

void MEchoService::OnAccept(uint64 ConnId, TSharedPtr<INetConnection> Conn) {
    // 服务器间入站（Gateway/Echo 互连）——挂 RPC 分发：
    // MT_FunctionCall → DispatchBackendServerCallPacketInbound（按 FunctionId 调
    // Echo/EchoAwait，响应回调用方）；MT_FunctionResponse → HandleServerCallResponse
    //（CallToActor 出站调用的响应回调）。
    //
    // P5: 改用基类 DispatchConnection 而非直接 EventLoop.RegisterConnection —
    // 单 Reactor 模式(N=0)走基类 EventLoop,多 Reactor 模式按 remote_addr
    // hash 派发到对应 Sub(由 MNetServerBase::DispatchConnection 内部处理)。
    DispatchConnection(
        ConnId, Conn,
        [this, Conn](uint64 /*Cid*/, const TByteArray& Payload) {
            if (Payload.empty()) {
                return;
            }
            const uint8 PacketType = Payload[0];
            TByteArray  Data(Payload.begin() + 1, Payload.end());
            if (static_cast<EServerMessageType>(PacketType) == EServerMessageType::MT_FunctionCall) {
                DispatchBackendServerCallPacketInbound(this, Conn, Data);
            } else if (static_cast<EServerMessageType>(PacketType) == EServerMessageType::MT_FunctionResponse) {
                HandleServerCallResponse(Data);
            }
        },
        [](uint64 /*Cid*/) {});
}

void MEchoService::ShutdownConnections() {
    // 关闭前保存 actor 持久化 state(覆盖式)
    MActorSystem::Get().SaveActorState(MRankListActor::RANK_LIST_ACTOR_ID, "Logs/ranklist_actor.bin");

    for (uint32 InstId : Config.LocalActorIds) {
        const uint64 ActorId = MServiceId::Make(Config.LocalServerType, InstId);
        MActorRouter::Get().UnregisterActor(ActorId);
    }
    ActorMessages.clear();

    // 注销 Registry 连接 + 清空 ConnectionPool
    MEndpointCache::Get().DeregisterAndShutdown();
}

void MEchoService::OnRunStarted() {
    LOG_INFO("%s running on port %u", Config.ServiceName.c_str(), static_cast<unsigned>(Config.ListenPort));
    // 启动时恢复 actor 持久化 state(Restaurant 排名等)
    // 文件存在 → 恢复;不存在 → 跳过(首次启动或 actor 不持久化)
    MActorSystem::Get().LoadActorState(MRankListActor::RANK_LIST_ACTOR_ID, "Logs/ranklist_actor.bin");
}

void MEchoService::RegisterLocalActors() {
    for (uint32 InstId : Config.LocalActorIds) {
        // 本机 Actor 标记 EServerType::Unknown——MActorRouter::SendToActor 在
        // ServerType == Unknown 时走 IsActorLocal 分支（见 ActorRouter.cpp:42-46）。
        const uint64 ActorId = MServiceId::Make(Config.LocalServerType, InstId);
        MActorRouter::Get().RegisterActor(ActorId, EServerType::Unknown);
        ActorMessages[ActorId] = MString();
        LOG_INFO("%s: registered local actor ServiceId=%u InstId=%u (ActorId=%llu)", Config.ServiceName.c_str(), static_cast<unsigned>(MServiceId::GetServiceId(ActorId)), static_cast<unsigned>(MServiceId::GetInstId(ActorId)),
                 static_cast<unsigned long long>(ActorId));
    }
}

// P3 v1 inline-body design: MEchoService::EchoAwait has its body in
// EchoService.h. The generated MHeaderTool_AsyncFrame_MEchoService_EchoAwait
// struct (MHeaderTool-generated) supplies the AwaitOk helper; AWAIT_OK
// expands to Frame->AwaitOk(expr). See spec 2026-07-24 §7.3.

SFutureResult<FSampleEchoResponse> MEchoService::Echo(const FSampleEchoRequest& Request) {
    if (Request.TargetActorId == 0) {
        return MServerCallAsyncSupport::MakeErrorFuture<FSampleEchoResponse>("actor_id_required", "Echo");
    }

    // 从 TargetActorId 拆解目标 Service 类型
    const EServerType TargetServiceType = MServiceId::GetServiceType(Request.TargetActorId);
    const uint32      TargetInstId      = MServiceId::GetInstId(Request.TargetActorId);

    LOG_INFO("%s: Echo received TargetServiceType=%s TargetInstId=%u", Config.ServiceName.c_str(), GetServerTypeDisplayName(TargetServiceType), static_cast<unsigned>(TargetInstId));

    // 响应中带回本进程的 (ServiceId, InstId) 让远端能反调
    const uint64 SelfActorId = MServiceId::Make(Config.LocalServerType, Config.LocalInstId);

    FSampleEchoResponse Response;
    Response.Echo             = Request.Message + " [echoed]";
    Response.SourceActorId    = SelfActorId; // 远端用此反查
    Response.SourceServerName = Config.ServiceName;

    // 本机 Actor：MActorRouter 命中本进程 → 直接返（避免走跨进程）
    if (MActorRouter::Get().IsActorLocal(Request.TargetActorId)) {
        return MServerCallAsyncSupport::MakeSuccessFuture(std::move(Response));
    }

    // 跨进程转发（链 2 路径：ServiceA → ServiceB 走 MRpcChannel::CallToActor）
    // MEndpointCache::GetOrConnect 走 Registry 推送的 endpoint 列表 + lazy connect。
    return MRpcChannel::Get().CallToActor<FSampleEchoResponse>(Request.TargetActorId, "MEchoService", "Echo", Request);
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

// P2 stub implementations for Rpc_OnServerHandshake / Rpc_OnHeartbeat.
// These were previously declared but not defined; P2 wires the ServerCall
// dispatch (Finding A+B) so generated handlers actually invoke them.
// Both are pure ack-style: zero-payload SEmptyServerMessage Ok on the wire.
MFuture<TResult<SEmptyServerMessage, FAppError>> MEchoService::Rpc_OnServerHandshake(uint32 /*DummyServerId*/) {
    SEmptyServerMessage Empty{};
    return MServerCallAsyncSupport::MakeSuccessFuture(std::move(Empty));
}

MFuture<TResult<SEmptyServerMessage, FAppError>> MEchoService::Rpc_OnHeartbeat(uint32 /*Seq*/) {
    SEmptyServerMessage Empty{};
    return MServerCallAsyncSupport::MakeSuccessFuture(std::move(Empty));
}

// 跨进程 actor 消息接收端 —— 由 MRpcChannel::CallActor 发过来的 FActorMessageWire
// 反射到这里,转成本进程内 FActorMessage 派发给目标 actor(走 MActorSystem::DispatchLocal,
// 由 system 负责把 OnMessage 投到 actor 自己的 Sub 线程,保证单线程访问契约)。
//
// 当前语义:Post(fire-and-forget)。Call(请求-响应)的远端回包链路留 TODO:
// 1) actor 端需要 ReplyPromise 跨进程传递(目前 ReplyPromise 不上 wire)
// 2) actor->SetValue 之后要主动 send ServerCallResponse 到 caller 进程
// 3) caller 进程 ReceiveServerCallResponse 时再 SetValue 对应 promise
SFutureResult<SEmptyServerMessage> MEchoService::OnActorMessage(const FActorMessageWire& InWire) {
    FActorMessage Msg;
    Msg.Header.SenderId    = InWire.SenderId;
    Msg.Header.TargetId    = InWire.TargetId;
    Msg.Header.MsgType     = InWire.MsgType;
    Msg.Header.PayloadSize = static_cast<uint32>(InWire.Payload.size());
    Msg.Payload            = InWire.Payload;
    // ReplyPromise 留空 —— Post 语义不期待回复。

    MActorSystem::Get().DispatchLocal(InWire.TargetId, Msg);

    SEmptyServerMessage Empty{};
    return MServerCallAsyncSupport::MakeSuccessFuture(std::move(Empty));
}

// 跨进程 actor Call 接收端:对端 MRpcChannel::CallActorAndWait 发 FActorMessageWire 过来,
// 1) 在本进程内构造 FActorMessage + ReplyPromise,投递到目标 actor 的 Sub 线程;
// 2) actor OnMessage 处理完后调 ReplyPromise->SetValue(响应字节);
// 3) 本函数用 Then 把 actor 回复字节包装成 FActorMessageWire 回包,沿 ServerCall 标准
//    响应通道回到 caller 进程,触发 MActorHandle::Call 的 Promise resolve。
//
// actor 卡死 / 不回包 → ServerCall 默认 5s 超时自动 err("server_call_timeout")。
SFutureResult<FActorMessageWire> MEchoService::OnActorCall(const FActorMessageWire& InWire) {
    FActorMessage Msg;
    Msg.Header.SenderId    = InWire.SenderId;
    Msg.Header.TargetId    = InWire.TargetId;
    Msg.Header.MsgType     = InWire.MsgType;
    Msg.Header.PayloadSize = static_cast<uint32>(InWire.Payload.size());
    Msg.Payload            = InWire.Payload;

    // 本进程内的 actor 回复 Promise。actor->SetValue 后这个 Promise resolve,
    // 由 .Then bridge 到外层 OuterPromise(->ServerCall response ->caller)。
    auto ReplyPromise = MakeShared<MPromise<TResult<TByteArray, FAppError>>>();
    Msg.ReplyPromise = ReplyPromise;

    // 外层 Promise:onActorCall 返回的 SFutureResult<FActorMessageWire> 等这个 resolve。
    auto OuterPromise = MakeShared<MPromise<TResult<FActorMessageWire, FAppError>>>();
    auto OuterFuture  = OuterPromise->GetFuture();

    // actor 处理完后:把 TByteArray 包成 FActorMessageWire(echo SenderId/TargetId/MsgType,
    // Payload 替换为 actor 响应字节)resolve 到 OuterPromise。
    ReplyPromise->GetFuture().Then([OuterPromise, InWire](MFuture<TResult<TByteArray, FAppError>> F) mutable {
        TResult<TByteArray, FAppError> R = F.Get();
        if (R.IsErr()) {
            OuterPromise->SetValue(TResult<FActorMessageWire, FAppError>::Err(R.GetError()));
            return;
        }
        FActorMessageWire Response;
        Response.SenderId = InWire.SenderId;
        Response.TargetId = InWire.TargetId;
        Response.MsgType  = InWire.MsgType;
        Response.Payload  = R.GetValue();
        OuterPromise->SetValue(TResult<FActorMessageWire, FAppError>::Ok(std::move(Response)));
    });

    // 投递到 actor 自己的 Sub 线程(等 Sub drain 时 actor OnMessage 才会跑)。
    MActorSystem::Get().DispatchLocal(InWire.TargetId, Msg);

    return OuterFuture;
}

// EchoAwait 的 await 目标（A 形态包装）——CallToActor 封装为可调用自由函数。
MEchoService* GGlobalEchoService = nullptr;

SFutureResult<FSampleEchoResponse> CallEchoRemote(const FSampleEchoRequest& Request, MEchoService* InService) {
    // 本地 actor 直返:若目标 ActorId 在本进程(ServerType=Unknown),
    // CallToActor → SendToActor → GetOrConnect(Unknown) 必然失败
    // (connection_unavailable + actor_route_invalid)。与 MEchoService::Echo
    // 的 IsActorLocal 分支一致 —— 本地调 Service->Echo 同步直返。
    if (InService != nullptr && MActorRouter::Get().IsActorLocal(Request.TargetActorId)) {
        return InService->Echo(Request);
    }
    return MRpcChannel::Get().CallToActor<FSampleEchoResponse>(Request.TargetActorId, "MEchoService", "Echo", Request);
}


// MFUNCTION(ServerCall, Async) 业务逻辑体（codegen 输入——方案 B：体在 .cpp）
#ifdef MESSION_AWAIT_CODEGEN_SOURCE
MFUNCTION(ServerCall, Async)
SFutureResult<FSampleEchoResponse> MEchoService::EchoAwait(const FSampleEchoRequest& Request) {
    return TAwaitable<CallEchoRemote>(Request, GGlobalEchoService);
}
#endif
