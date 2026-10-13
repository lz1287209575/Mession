/**
 * @file PlayerService.h
 * @brief MPlayerService —— Player 业务进程(每玩家 actor 容器,见
 *   Docs/superpowers/specs/2026-08-14-player-design.md §2)。
 *
 * @detail 本类零业务协议:业务协议下沉到 ActorMember(MPlayerItemContainer 等),进程侧的
 *   请求接收由框架成员分发器完成(FrameworkMemberDispatch -> DispatchFrameworkMemberCall,
 *   实现见 Common/Runtime/Actor/ActorMember.cpp)。本类只负责 actor 集合与进程骨架。
 */
#pragma once

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

/**
 * @brief FMemberDispatchRequest - 框架成员分发入口的请求体。
 *
 * @detail 生成的 ServerCall handler 只支持单参数,故把 (FunctionId, Payload) 打包进一个
 *   MSTRUCT:FunctionId 是成员方法的稳定 id,Payload 是成员请求的反射字节流(如
 *   FPlayerUseItemRequest)。客户端经 Gateway 按 manifest 的 TargetName("Player") 路由到
 *   本进程,再走 FrameworkMemberDispatch -> 框架分发器(查 GMemberRpcEntries -> 反序列化
 *   取 PlayerId -> MActorSystem::Find -> actor 宿主成员表 -> 成员反射调用)。
 */
MSTRUCT()
struct FMemberDispatchRequest {
    MPROPERTY()
    uint16 FunctionId = 0;

    MPROPERTY()
    TByteArray Payload;
};

/** @brief SPlayerServiceConfig - MPlayerService 的 CLI 配置(MSTRUCT + MPROPERTY,由反射解析)。 */
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

/**
 * @brief MPlayerService - Player 业务进程:持有玩家 actor 集合,转发框架成员调用。
 *
 * @detail 生命周期由 MServiceMain::Run<MPlayerService, SPlayerServiceConfig> 驱动;
 *   单例指针 GGlobalPlayerService 只在 .cpp 内可见,对外经 GetSingleton() 访问。
 */
MCLASS(Type = Service)
class MPlayerService : public MNetServerBase, public MObject {
    public:
    MGENERATED_BODY(MPlayerService, MObject, 0)
    public: // MGENERATED_BODY 展开以 private: 结尾,恢复 public 访问
    using MObject::Tick;

    /** @brief GetSingleton - 返回本进程单例(Init() 之前为 nullptr)。 */
    static MPlayerService* GetSingleton();

    /**
     * @brief Init - 读配置、装配多 Reactor、注册 Registry 与本地 actor,并创建演示玩家 actor。
     * @param InPort 覆盖配置里的 ListenPort(>0 时生效;0 = 用配置值)
     * @return true 成功;false = ListenPort 为 0,或 --registry 缺失 / 格式不是 host:port
     */
    bool Init(int InPort = 0);
    void Run() override {
        MNetServerBase::Run();
    }

    uint16 GetListenPort() const override;
    void   OnAccept(uint64 ConnId, TSharedPtr<INetConnection> Conn) override;
    void   TickBackends() override;
    void   ShutdownConnections() override;
    void   OnRunStarted() override;

    /**
     * @brief FrameworkMemberDispatch - 框架成员分发入口(纯框架机制,不含业务协议)。
     * @param Req.FunctionId 成员方法稳定 id
     * @param Req.Payload    成员请求的反射字节流
     * @return 成员方法响应序列化后的字节;FunctionId 无法解析或玩家不存在时由框架分发器给出错误
     *
     * @detail 声明里带 Target="Player",manifest 条目据此带 TargetName,Gateway 按它把请求路由到
     *   本进程。业务逻辑在 ActorMember 上,本函数只做转发。
     */
    MFUNCTION(ServerCall, Target = "Player")
    SFutureResult<TByteArray> FrameworkMemberDispatch(const FMemberDispatchRequest& Req);

    /**
     * @brief RegisterPlayer - 测试/验证入口:创建玩家 actor 并挂载 MPlayerItemContainer。
     * @param PlayerId 玩家 id(同时作为 ActorId);为 0 直接返回错误
     * @return 空 Ok;失败时 Err("player_id_required" / "player_already_online")
     *
     * @detail 正式形态应由登录/会话激活驱动创建,当前 PoC 由本入口或 Init() 里的演示代码触发。
     */
    MFUNCTION(ServerCall)
    SFutureResult<SEmptyServerMessage> RegisterPlayer(uint64 PlayerId);

    private:
    /** @brief RegisterLocalActors - 把 --actors 声明的本机 ActorId 注册到 MActorRouter
     *         (以 ServerType=Unknown 标记为本机)。 */
    void RegisterLocalActors();

    SPlayerServiceConfig Config;

    // 本进程创建的玩家 actor 的强引用表:持有所有权,避免 actor 只因 MActorSystem 内部
    // 引用而不可控;ShutdownConnections() 里 clear 释放。
    TVector<TSharedPtr<MPlayerActor>> PlayerActors;
};
