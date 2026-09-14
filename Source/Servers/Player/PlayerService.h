#pragma once

// MPlayerService — Player 业务进程(2026-08-14 player-design §2)。
//
// 零业务协议:业务协议下沉到 ActorMember(MPlayerItemContainer 等),进程接收
// 由框架成员分发器完成(FrameworkMemberDispatch → DispatchFrameworkMemberCall,
// Common/Runtime/Actor/ActorMember.cpp)。本类只持 actor 集合 + 进程骨架。

#include "Common/IO/Socket/Socket.h"
#include "Common/Net/NetServerBase.h"
#include "Common/Net/Routing/ActorRouter.h"
#include "Common/Runtime/Actor/ActorMember.h"
#include "Common/Runtime/Actor/IActor.h"
#include "Common/Runtime/Actor/MActorSystem.h"
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
#include "Servers/App/ServerCallAsyncSupport.h"
#include "Servers/App/ServiceId.h"
#include "Servers/App/ServiceMain.h"
#include "Servers/Player/MPlayerActor.h"

// F0→PlayerService 框架成员分发入口请求:FunctionId(成员方法稳定 id)+ Payload
// (成员请求反射字节流,如 FPlayerUseItemRequest)。生成器 ServerCall handler
// 只支持单参数,所以打包进一个 MSTRUCT。客户端经 Gateway 按 manifest
// TargetName("Player") 路由到本进程 → FrameworkMemberDispatch → 框架分发器
// (查 GMemberRpcEntries → 反序列化取 PlayerId → MActorSystem::Find →
// actor 宿主成员表 → 成员反射调用)。
MSTRUCT()
struct FMemberDispatchRequest {
    MPROPERTY()
    uint16 FunctionId = 0;

    MPROPERTY()
    TByteArray Payload;
};

MSTRUCT()
struct SPlayerServiceConfig {
    MPROPERTY(Meta = (Cli = "--listen"))
    uint16 ListenPort = 0;

    MPROPERTY(Meta = (Cli = "--service"))
    MString ServiceName = "MPlayerService";

    // --local-type 接受字符串("Player"),反射到 LocalServerType 后再派生 LocalServerId。
    MPROPERTY(Meta = (Cli = "--local-type"))
    MString LocalServerTypeName = "Player";

    MPROPERTY()
    EServerType LocalServerType = EServerType::Unknown;

    MPROPERTY(Meta = (Cli = "--server-id"))
    uint32 LocalServerId = 0;

    MPROPERTY(Meta = (Cli = "--inst"))
    uint32 LocalInstId = 0;

    MPROPERTY(Meta = (Cli = "--actors"))
    TVector<uint32> LocalActorIds;

    // 多 Reactor Sub 线程数(0 = 禁用,走单 Reactor)。默认 4。
    MPROPERTY(Meta = (Cli = "--subs"))
    uint32 SubCount = 4;

    // Service 注册中心地址("127.0.0.1:18000")。PoC 阶段强制要求传。
    MPROPERTY(Meta = (Cli = "--registry"))
    MString RegistryAddr;

    // 日志:ServiceMain::Run 会读这些字段初始化 MLog 管道。
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
class MPlayerService : public MNetServerBase, public MObject {
    public:
    MGENERATED_BODY(MPlayerService, MObject, 0)
    public: // MGENERATED_BODY 展开以 private: 结尾,恢复 public 访问
    using MObject::Tick;

    static MPlayerService* GetSingleton();

    bool Init(int InPort = 0);
    void Run() override {
        MNetServerBase::Run();
    }

    uint16 GetListenPort() const override;
    void   OnAccept(uint64 ConnId, TSharedPtr<INetConnection> Conn) override;
    void   TickBackends() override;
    void   ShutdownConnections() override;
    void   OnRunStarted() override;

    // 框架成员分发入口(Target="Player":manifest 条目带 TargetName,Gateway
    // 按它路由到本进程)。业务类零业务协议——协议在 ActorMember 上。
    MFUNCTION(ServerCall, Target = "Player")
    SFutureResult<TByteArray> FrameworkMemberDispatch(const FMemberDispatchRequest& Req);

    // 测试/验证入口:创建玩家 actor(PlayerId)+ 挂载成员,注册进 MActorSystem。
    MFUNCTION(ServerCall)
    SFutureResult<SEmptyServerMessage> RegisterPlayer(uint64 PlayerId);

    private:
    void RegisterLocalActors();

    SPlayerServiceConfig Config;
    TVector<TSharedPtr<MPlayerActor>> PlayerActors;
};
