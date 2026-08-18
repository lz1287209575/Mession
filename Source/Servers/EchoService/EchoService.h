#pragma once

#include "Common/IO/Socket/Socket.h"
#include "Common/Net/NetServerBase.h"
#include "Common/Net/Routing/ActorRouter.h"
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
