/**
 * @file EchoService.h
 * @brief MEchoService —— Echo 业务进程:ServerCall 回显 + 排行榜 actor + 跨进程 actor 消息收发。
 *
 * @detail 本类同时是框架的演练场,用于端到端验证:
 *   - 同步 / 异步 ServerCall(MFUNCTION(ServerCall) 与 MFUNCTION(ServerCall, Async));
 *   - Client 下行通知(MFUNCTION(CallClient) + Gateway PushClientDownlink);
 *   - 运行时动态注册 actor 并上报 Registry,供跨实例路由命中;
 *   - 跨进程 actor 消息(Post / Call)的反射落地入口。
 *
 * @detail 生命周期:由 MServiceMain::Run<MEchoService, SEchoServiceConfig> 驱动
 *   (Init -> Run -> Shutdown),实例由 TSharedPtr 持有。GGlobalEchoService 是本进程单例,
 *   专供 Async 业务体(codegen 生成的 Frame)使用 —— 那里拿不到 this。
 *
 * @detail actor 单线程契约:所有 actor 消息经 MActorSystem 投递到 actor 自己的 Sub 线程,
 *   业务回调里不要跨线程直接读写 actor 状态。
 */
#pragma once

#include "Common/IO/Socket/Socket.h"
#include "Common/Net/NetServerBase.h"
#include "Common/Net/Routing/ActorRouter.h"
#include "Common/Net/Rpc/MRpcChannel.h"
#include "Common/Runtime/Actor/FActorMessage.h"
#include "Common/Runtime/Actor/IActor.h"
#include "Common/Runtime/Async/AwaitMacros.h"
#include "Common/Runtime/Async/Awaitable.h"
#include "Common/Runtime/Async/MAsync.h"
#include "Common/Runtime/Id.h"
#include "Common/Runtime/Log/Log.h"
#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Object/Object.h"
#include "Common/Runtime/Object/Result.h"
#include "Common/Runtime/Reflect/Reflection.h"
#include "Protocol/Messages/Common/AppMessages.h"
#include "Protocol/Messages/Common/ControlPlaneMessages.h"
#include "Protocol/Messages/EchoService/FSampleEchoMessages.h"
#include "Servers/App/ServerCallAsyncSupport.h"
#include "Servers/App/ServiceId.h"
#include "Servers/App/ServiceMain.h"

// P3 v1 inline-body 设计:生成的 per-class Frame 结构体
//(`MHeaderTool_AsyncFrame_MEchoService_EchoAwait`)放在独立的生成头文件里,
// 而不是 MEchoService.mgenerated.h。原因:MEchoService.mgenerated.h 会 include 本用户头,
// 若这里反向 include 它就形成循环包含;AsyncFrames 头是单向的(本头包含它,它不包含本头)。
// Frame 结构体必须位于 FSampleEchoMessages.h 之后(ReqType/RespType 可见)、
// class MEchoService 之前(内联 async 体要引用 Frame)。
// NOTE: 此处原引用的设计文档 Docs/superpowers/specs/2026-07-24-cpp17-async-await.md
//       已不在仓库中(仅保留结论描述)。
#include "MEchoService_AsyncFrames.h"

class MEchoService;
extern MEchoService* GGlobalEchoService;

// EchoAwait 的 await 目标(A 形态:TAwaitable<F, Args...> 里的 F)——把 CallToActor 包成可 await 的调用。
// 只在此声明,供 #ifdef MESSION_AWAIT_CODEGEN_SOURCE 段内的 await 表达式引用;定义在 EchoService.cpp。
SFutureResult<FSampleEchoResponse> CallEchoRemote(const FSampleEchoRequest& Request, MEchoService* InService);


/** @brief SEchoServiceConfig - MEchoService 的 CLI 配置(MSTRUCT + MPROPERTY,由反射解析)。 */
MSTRUCT()
struct SEchoServiceConfig {
    MPROPERTY(Meta = (Cli = "--listen"))
    uint16 ListenPort = 0;

    MPROPERTY(Meta = (Cli = "--service"))
    MString ServiceName = "MEchoService";

    // --local-type 接受字符串（"Echo" / "Gateway"），反射到 LocalServerType 后再派生 LocalServerId。
    MPROPERTY(Meta = (Cli = "--local-type"))
    MString LocalServerTypeName = "Echo";

    // 后处理派生字段:反射解析器只填 LocalServerTypeName,本字段由 Init() 调
    // MServiceMain::ParseLocalServerType 写回。
    MPROPERTY()
    EServerType LocalServerType = EServerType::Unknown;

    MPROPERTY(Meta = (Cli = "--server-id"))
    uint32 LocalServerId = 0;

    MPROPERTY(Meta = (Cli = "--inst"))
    uint32 LocalInstId = 0;

    MPROPERTY(Meta = (Cli = "--actors"))
    TVector<uint32> LocalActorIds;

    // 多 Reactor Sub 线程数(0 = 禁用,走单 Reactor)。默认 4——按核数(96)
    // 启动 96 个 Sub 线程会让空闲服务空转吃掉 ~19% CPU(曾实测 1500% 忙转,
    // 修复后仍残留 96 线程唤醒开销)。
    MPROPERTY(Meta = (Cli = "--subs"))
    uint32 SubCount = 4;

    // Service 注册中心地址（"127.0.0.1:18000"）。PoC 阶段强制要求传
    // ——MEndpointCache 启动期就要连 Registry 取全量 endpoint。
    MPROPERTY(Meta = (Cli = "--registry"))
    MString RegistryAddr;

    // 日志：ServiceMain::Run 会读这些字段初始化 MLog 管道。
    MPROPERTY(Meta = (Cli = "--log-file"))
    MString LogFilePath = "";

    MPROPERTY(Meta = (Cli = "--log-rotate-bytes"))
    uint64 LogRotateBytes = 100ull * 1024ull * 1024ull;

    MPROPERTY(Meta = (Cli = "--log-archives"))
    uint32 LogArchives = 5;

    MPROPERTY(Meta = (Cli = "--log-config"))
    MString LogConfigPath = "";
};

/**
 * @brief MDynamicTestActor - 动态 actor 测试目标:被 Call 时把收到的 payload 原样回给调用方。
 *
 * @detail ActorId 由调用方传入(可变),用于验证运行时注册(RegisterDynamicActor)后
 *   本进程 / 跨实例的 actor 路由能否命中。仅在 ReplyPromise 非空(即 Call 路径)时回复。
 */
class MDynamicTestActor : public IActor {
    public:
    explicit MDynamicTestActor(uint64 InActorId) : ActorId(InActorId) {
    }

    uint64 GetActorId() const override {
        return ActorId;
    }

    void OnMessage(const FActorMessage& InMsg) override {
        // Call 路径:把收到的 payload 原样回给调用方(ReplyPromise 非空时)。
        if (InMsg.ReplyPromise != nullptr) {
            InMsg.ReplyPromise->SetValue(TResult<TByteArray, FAppError>::Ok(InMsg.Payload));
        }
    }

    private:
    uint64 ActorId = 0;
};

/**
 * @brief MEchoService - Echo 业务进程(ServerCall 回显 / actor 收发演练)。
 * @detail 生命周期与线程模型见文件头说明;配置来自 SEchoServiceConfig 单例。
 */
MCLASS(Type = Service)
class MEchoService : public MNetServerBase, public MObject {
    public:
    MGENERATED_BODY(MEchoService, MObject, 0)
    public:
    using MObject::Tick;

    /** @brief GetSingleton - 返回本进程单例 GGlobalEchoService(Init() 之前为 nullptr)。 */
    static MEchoService* GetSingleton() {
        return GGlobalEchoService;
    }

    /**
     * @brief Init - 读配置、装配多 Reactor、连接并注册 Registry、注册本地 actor。
     * @param InPort 覆盖配置里的 ListenPort(>0 时生效;0 = 用配置值)
     * @return true 成功;false = ListenPort 为 0,或 --registry 缺失 / 格式不是 host:port
     */
    bool Init(int InPort = 0);

    /** @brief Tick - 主线程每帧钩子(由 MNetServerBase::Run 主循环驱动)。 */
    void Tick();
    void Run() override {
        MNetServerBase::Run();
    }

    /** @brief GetListenPort - 返回配置里的监听端口。 */
    uint16 GetListenPort() const override;

    /** @brief OnAccept - 服务器间入站连接(Gateway/Echo 互连)的 RPC 分发入口。 */
    void OnAccept(uint64 ConnId, TSharedPtr<INetConnection> Conn) override;

    /** @brief TickBackends - 每帧维护 MEndpointCache(Registry 心跳发送 + 断线重连)。 */
    void TickBackends() override;

    /** @brief ShutdownConnections - 退出前落盘 actor 状态、注销本地 actor 与 Registry 连接。 */
    void ShutdownConnections() override;

    /** @brief OnRunStarted - listener 就绪后的回调:恢复 actor 持久化状态。 */
    void OnRunStarted() override;

    /**
     * @brief Echo - 同步 ServerCall:回显请求,并在响应里带上本进程的 (ServiceType, InstId)。
     * @param Request 回显请求;TargetActorId 高 32 位是目标服务类型、低 32 位是实例号
     * @return 响应 future:目标 actor 在本进程时直接给出;否则经 MRpcChannel::CallToActor 跨进程转发
     *
     * @detail Gateway 通过 MClientManifest 把 Client_* 请求转到本类后调用。
     */
    MFUNCTION(ServerCall)
    SFutureResult<FSampleEchoResponse> Echo(const FSampleEchoRequest& Request);

    /**
     * @brief EchoAwait - Echo 的 await 形态:把跨进程调用包成可挂起的 A 形态 awaitable。
     * @param Request 回显请求(语义同 Echo)
     * @return 响应 future;await 未就绪时由生成的状态机挂起、就绪后恢复
     *
     * @detail P3 基于 Frame 的 async demo。业务体不写在本声明处,而是写在 EchoService.cpp 里
     *   由 `#ifdef MESSION_AWAIT_CODEGEN_SOURCE` 包住的段内(A 形态:
     *   `return TAwaitable<CallEchoRemote>(Request, GGlobalEchoService);`)。
     *   MHeaderTool 解析该体,生成 Build/Generated/MEchoService_AwaitImpl.mgenerated.cpp
     *   (成员实现)与 MEchoService_AwaitStateMachine.h(await 状态机 Frame)。
     *   没有 `Awaits=...` 元数据:await 目标就是业务体里的表达式文本,而非宏标签。
     */
    MFUNCTION(ServerCall, Async)
    SFutureResult<FSampleEchoResponse> EchoAwait(const FSampleEchoRequest& Request);

    /**
     * @brief OnActorMessage - 跨进程 actor Post(fire-and-forget)的接收端。
     * @param InWire 对端经 MRpcChannel::CallActor 发来的 actor 消息字节
     * @return 空 ack(SEmptyServerMessage);不等 actor 处理结果
     *
     * @detail 收到后转成本进程 FActorMessage 投给 MActorSystem::DispatchLocal 并立即返回;
     *   目标 actor 的 OnMessage 何时跑取决于它所在 Sub 线程的 drain 时机。
     *   是 actor-extension 设计阶段 4 与 multi-reactor 设计阶段 6 的配套入口。
     */
    MFUNCTION(ServerCall)
    SFutureResult<SEmptyServerMessage> OnActorMessage(const FActorMessageWire& InWire);

    /**
     * @brief OnActorCall - 跨进程 actor Call(请求-响应)的接收端。
     * @param InWire 对端经 MRpcChannel::CallActorAndWait 发来的 actor 消息字节
     * @return future:等目标 actor 经 ReplyPromise 给出响应字节后 resolve
     *
     * @detail 本地 actor 的 OnMessage 调 Msg.ReplyPromise->SetValue(字节) 后,本函数把字节
     *   包成 FActorMessageWire 回给对端。actor 若一直不回包,ServerCall 默认 5s 超时,
     *   调用方拿到 err("server_call_timeout")。
     */
    MFUNCTION(ServerCall)
    SFutureResult<FActorMessageWire> OnActorCall(const FActorMessageWire& InWire);

    /**
     * @brief NotifyEvent - 下行通知声明(MFUNCTION(CallClient)):服务端 -> 客户端的单向推送。
     * @param Msg 通知内容
     *
     * @detail 生成器为它分配下行 FunctionId(MDownlink_MEchoService_NotifyEvent,见
     *   MClientDownlinkManifest.mgenerated.h)。服务端业务不直接调本方法体,而是经
     *   Gateway PushClientDownlink 把消息推给客户端。
     */
    MFUNCTION(CallClient)
    void NotifyEvent(const FNotifyEventMsg& Msg);

    /**
     * @brief TriggerNotify - 测试入口:客户端调用它触发一次下行广播。
     * @param Req.Text 要广播的文本
     * @return 空 ack;广播本身经 Gateway 推给全部在线客户端(FunctionId = NotifyEvent 的下行 id)
     */
    MFUNCTION(ServerCall)
    SFutureResult<SEmptyServerMessage> TriggerNotify(const FTriggerNotifyRequest& Req);

    /**
     * @brief RegisterDynamicActor - 测试入口:运行时动态注册一个 MDynamicTestActor。
     * @param ActorId 实例号(低位);高位由本进程 LocalServerType 补齐
     * @return 空 ack
     *
     * @detail 注册会同时进本进程 MActorRouter 本地路由(IsActorLocal 命中)并触发进程级
     *   ActorIds 上报 Registry;对端收到 EndpointChange 后即可跨实例 CallToActor 命中。
     */
    MFUNCTION(ServerCall)
    SFutureResult<SEmptyServerMessage> RegisterDynamicActor(uint32 ActorId);

    /**
     * @brief Rpc_OnServerHandshake - 传输层握手桩:无状态 ack。
     * @param DummyServerId 占位参数(见签名限制),实现里被忽略
     * @return 空 Ok 响应
     *
     * @detail MServerConnection::SendHandshake 经 MRpc::CallRemote 调到这里。
     *   签名限制:MHeaderTool 目前生成的 ServerCall stub 只支持 1 参数 invoke
     *   (GenerateServerCallHandler 跳过 0 参数函数、且只解析第一个参数),所以用单个
     *   uint32 当 dummy —— handshake 本身无状态,该参数没有语义。
     */
    MFUNCTION(ServerCall)
    MFuture<TResult<SEmptyServerMessage, FAppError>> Rpc_OnServerHandshake(uint32 DummyServerId);

    /** @brief Rpc_OnHeartbeat - 心跳桩:无状态 ack。
     *  @param Seq 占位参数(签名限制同 Rpc_OnServerHandshake),实现里被忽略 */
    MFUNCTION(ServerCall)
    MFuture<TResult<SEmptyServerMessage, FAppError>> Rpc_OnHeartbeat(uint32 Seq);

    private:
    /** @brief RegisterLocalActors - 把配置里 --actors 声明的本机 ActorId 注册到 MActorRouter
     *         (以 ServerType=Unknown 标记为本机)。 */
    void RegisterLocalActors();

    SEchoServiceConfig Config;

    // NOTE: ActorMessages 只在 RegisterLocalActors() 里写空串、在 ShutdownConnections() 里
    //       clear,从未被读取 —— 疑似早期设计的残留(死状态)。此处保留不动,仅作记录。
    TMap<uint64, MString> ActorMessages;
};
