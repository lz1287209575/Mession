#pragma once

#include "Common/IO/Socket/Socket.h"
#include "Common/Net/NetServerBase.h"
#include "Common/Net/Routing/ActorRouter.h"
#include "Common/Runtime/Actor/IActor.h"
#include "Common/Net/Rpc/MRpcChannel.h"
#include "Common/Runtime/Actor/FActorMessage.h"
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
#include "Servers/EchoService/Members/MEchoPlayerActor.h"
#include "Servers/EchoService/Members/MPlayerItemContainer.h"

// P3 v1 inline-body design: the generated Frame struct
// (`MHeaderTool_AsyncFrame_MEchoService_EchoAwait`) lives in a separate
// generated header, NOT the per-class MEchoService.mgenerated.h. Reason:
// MEchoService.mgenerated.h includes this user header, so including
// MEchoService.mgenerated.h from here would create a circular include.
// The AsyncFrames header is one-way: this header includes it, but it
// does NOT include this header back. The Frame struct must come AFTER
// FSampleEchoMessages.h (so ReqType/RespType are visible) and BEFORE
// class MEchoService (so the inline async body can reference the Frame).
#include "MEchoService_AsyncFrames.h"

class MEchoService;
extern MEchoService* GGlobalEchoService;

// EchoAwait 的 await 目标（A 形态：TAwaitable<F,R,Args...> 的 F）——包装 CallToActor 为可调用。
// 声明供 #ifdef 体内的 await 表达式引用；定义在 EchoService.cpp。
SFutureResult<FSampleEchoResponse> CallEchoRemote(const FSampleEchoRequest& Request, MEchoService* InService);


MSTRUCT()
struct SEchoServiceConfig {
    MPROPERTY(Meta = (Cli = "--listen"))
    uint16 ListenPort = 0;

    MPROPERTY(Meta = (Cli = "--service"))
    MString ServiceName = "MEchoService";

    // --local-type 接受字符串（"Echo" / "Gateway"），反射到 LocalServerType 后再派生 LocalServerId。
    MPROPERTY(Meta = (Cli = "--local-type"))
    MString LocalServerTypeName = "Echo";

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

// 动态 actor 测试目标(可变 ActorId,Call 回显响应)——验证运行时注册
// (RegisterDynamicActor)后本进程/跨实例 actor 路由命中。
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

// F0 ActorMember 框架验证入口请求:FunctionId(成员方法稳定 id)+ Payload
// (成员请求反射字节流,如 FPlayerUseItemRequest)。生成器 ServerCall handler
// 只支持单参数,所以 FunctionId/Payload 打包进一个 MSTRUCT。
// 客户端经 Gateway 按 FunctionId 路由到本服务进程 → FrameworkMemberDispatch
// → 框架成员分发器(DispatchFrameworkMemberCall, Common/Runtime/Actor/
// ActorMember.cpp):查 GMemberRpcEntries → 反序列化取 PlayerId →
// MActorSystem::Find(PlayerId) → actor 宿主 → 成员 → 反射调用。
MSTRUCT()
struct FMemberDispatchRequest {
    MPROPERTY()
    uint16 FunctionId = 0;

    MPROPERTY()
    TByteArray Payload;
};

MCLASS(Type = Service)
class MEchoService : public MNetServerBase, public MObject {
    public:
    MGENERATED_BODY(MEchoService, MObject, 0)
    public:
    using MObject::Tick;

    static MEchoService* GetSingleton() {
        return GGlobalEchoService;
    }

    bool Init(int InPort = 0);
    void Tick();
    void Run() override {
        MNetServerBase::Run();
    }

    uint16 GetListenPort() const override;
    void   OnAccept(uint64 ConnId, TSharedPtr<INetConnection> Conn) override;
    void   TickBackends() override;
    void   ShutdownConnections() override;
    void   OnRunStarted() override;

    // EchoService 暴露的 ServerCall——Gateway 通过 MClientManifest 把 Client_* 转到本类后调用。
    MFUNCTION(ServerCall)
    SFutureResult<FSampleEchoResponse> Echo(const FSampleEchoRequest& Request);

    // P3: Frame-based async demo — user body constructs the generated Frame
    // inline, sets its context (Service/Request/Ctx), and returns AWAIT_OK.
    // No `Awaits=...` metadata: the await target is the user's expression
    // text, not a macro tag (per spec 2026-07-24 §7.3 v1 inline-body design).
    MFUNCTION(ServerCall, Async)
    SFutureResult<FSampleEchoResponse> EchoAwait(const FSampleEchoRequest& Request);

    // 跨进程 actor 消息接收端(Post):对端 MRpcChannel::CallActor 把 FActorMessageWire
    // 发到这里,本函数 dispatch 到本进程内 MActorSystem::DispatchLocal 后立即返回。
    // actor-extension spec 阶段 4 + multi-reactor spec 阶段 6 配套入口。
    MFUNCTION(ServerCall)
    SFutureResult<SEmptyServerMessage> OnActorMessage(const FActorMessageWire& InWire);

    // 跨进程 actor Call(请求-响应):对端 MRpcChannel::CallActorAndWait 走这里。
    // 返回 SFutureResult<FActorMessageWire> 等待本地 actor 回复;actor OnMessage
    // 通过 Msg.ReplyPromise->SetValue(...) 触发本函数 resolve,把回复字节带回对端。
    MFUNCTION(ServerCall)
    SFutureResult<FActorMessageWire> OnActorCall(const FActorMessageWire& InWire);

    // 下行通知声明（MFUNCTION(CallClient)）——生成器分配下行 FunctionId
    // （MDownlink_MEchoService_NotifyEvent，见 MClientDownlinkManifest.mgenerated.h），
    // 服务端业务经 Gateway PushClientDownlink 把该消息推送到客户端。
    MFUNCTION(CallClient)
    void NotifyEvent(const FNotifyEventMsg& Msg);

    // 测试入口：客户端调用它触发一次下行广播（NotifyEvent 发给全部在线客户端）。
    MFUNCTION(ServerCall)
    SFutureResult<SEmptyServerMessage> TriggerNotify(const FTriggerNotifyRequest& Req);

    // 测试入口：运行时动态注册 actor（MDynamicTestActor）——验证动态 actor
    // 上报 Registry 后本进程/跨实例路由命中。
    MFUNCTION(ServerCall)
    SFutureResult<SEmptyServerMessage> RegisterDynamicActor(uint32 ActorId);

    // F0 ActorMember 框架成员分发入口（PoC 验证载体）——框架内部机制，
    // 业务类零业务协议：FunctionId → GMemberRpcEntries → 请求取 PlayerId
    // → actor 宿主 → 成员反射调用。未来 MPlayerService 等业务进程同样
    // 继承 MNetServerBase + MObject 声明这一个入口即可获得成员分发能力。
    MFUNCTION(ServerCall)
    SFutureResult<TByteArray> FrameworkMemberDispatch(const FMemberDispatchRequest& Req);

    // 传输层握手 / 心跳桩——MServerConnection::SendHandshake / SendHeartbeat 会通过
    // MRpc::CallRemote 调 Rpc_OnServerHandshake / Rpc_OnHeartbeat。
    //
    // 签名限制：MHeaderTool 当前生成的 ServerCall stub 只支持 1-arg invoke
    // （GenerateServerCallHandler 跳过 0-arg 函数 + 只 parse 第一个参数），所以这里
    // 用单个 uint32 当 dummy 参数——handshake / heartbeat 是无状态 ack。
    MFUNCTION(ServerCall)
    MFuture<TResult<SEmptyServerMessage, FAppError>> Rpc_OnServerHandshake(uint32 DummyServerId);

    MFUNCTION(ServerCall)
    MFuture<TResult<SEmptyServerMessage, FAppError>> Rpc_OnHeartbeat(uint32 Seq);

    private:
    // 本机 Actor 注册到 MActorRouter（ServerType=Unknown 表示本机）。
    void RegisterLocalActors();

    SEchoServiceConfig    Config;
    TMap<uint64, MString> ActorMessages;
};
