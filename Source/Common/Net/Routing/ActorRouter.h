/**
 * @file ActorRouter.h
 * @brief ActorRouter - actor 路由表(谁在哪) + 跨进程 actor 调用入口。
 *
 * @detail 本类只维护**路由表**(ActorId → 多条 SActorRoute),actor 对象的实际所有权
 *   委托给 MActorSystem 单例(见 RegisterActor(TSharedPtr<IActor>, ...)),以避免双份
 *   所有权与双份锁。
 *
 *   并发:路由表由 RoutesMutex 保护,可从任意线程读写;连接解析依赖 MEndpointCache。
 */

#pragma once

#include "Common/Net/Routing/ActorRoute.h"
#include "Common/Net/Rpc/RpcServerCall.h"
#include "Common/Net/ServiceDiscovery/EndpointCache.h"
#include "Common/Runtime/Actor/MActorHandle.h"
#include "Common/Runtime/Async/MAsync.h"
#include "Common/Runtime/Reflect/Reflection.h"

#include <mutex>

class IActor;
class MActorHandle;
class MSubReactorPool;

/**
 * @brief MActorRouter - 进程内单例的 actor 路由表。
 */
class MActorRouter {
    public:
    /**
     * @brief Get - 取进程内单例(Meyers 单例,首次调用时构造)。
     */
    static MActorRouter& Get();

    /**
     * @brief RegisterActor - 追加一条 (ServerType, ConnectionId) 路由。
     * @param ActorId      全局 actor id
     * @param ServerType   该 actor 所在服务类型(Unknown = 本进程)
     * @param ConnectionId 目标连接 id(0 表示由调用方约定/本地)
     *
     * @detail 同一 (ActorId, ServerType, ConnectionId) 已存在时直接返回,不会重复追加;
     *   重复注册不同 ConnectionId 会形成 1:N 的多条路由。
     */
    void RegisterActor(uint64 ActorId, EServerType ServerType, uint64 ConnectionId = 0);

    /**
     * @brief UnregisterActor - 删除该 actor 的**全部**路由。
     * @param ActorId 全局 actor id;不存在时为空操作
     */
    void UnregisterActor(uint64 ActorId);

    /**
     * @brief UnregisterRoute - 只删除匹配 (ServerType, ConnectionId) 的那一条路由。
     * @param ActorId      全局 actor id
     * @param ServerType   服务类型
     * @param ConnectionId 连接 id
     *
     * @detail 删完最后一条时会把 ActorId 条目一并从表中移除。典型用途:某条出站
     *   连接断开时,只清掉挂在该连接上的路由,保留其它 endpoint 上的同 id actor。
     */
    void UnregisterRoute(uint64 ActorId, EServerType ServerType, uint64 ConnectionId);

    /**
     * @brief UpdateActorRoute - 刷新路由。
     * @param ActorId      全局 actor id
     * @param ServerType   服务类型
     * @param ConnectionId 连接 id
     *
     * @detail 当前实现就是 RegisterActor:没有时间戳/过期语义,所谓"更新"只是保证存在。
     */
    void UpdateActorRoute(uint64 ActorId, EServerType ServerType, uint64 ConnectionId = 0);

    /**
     * @brief CollectLocalActorIds - 收集本进程"本地 actor"(ServerType == Unknown 的路由)的 id。
     * @param OutActorIds 输出容器;**追加**语义,调用方负责先清空;同一 id 最多追加一次
     *
     * @detail 供进程级 ActorIds 全量上报使用(即 MActorRouter 直接注册的业务 actor,
     *   如 EchoService 的静态 actor)。顺序取决于 TMap 遍历顺序,不保证稳定。
     */
    void CollectLocalActorIds(TVector<uint64>& OutActorIds) const;

    /**
     * @brief FindActor - 查该 actor 的第一条路由。
     * @param ActorId 全局 actor id
     * @return 命中返回该路由;不存在时返回默认构造(此时 ActorId == 0,调用方按此判空)
     *
     * @detail 只取首条是为向后兼容;1:N 场景请用 FindAllActorRoutes。
     */
    SActorRoute FindActor(uint64 ActorId) const;

    /**
     * @brief FindAllActorRoutes - 取 actor 全部 route（用于 1:N fan-out）.
     *
     * 一个 ActorId 可对应多个 endpoint（不同 ServerType 各自的连接）—— 这是
     * 1:N fan-out 的基础。SendActor 会遍历此列表,每个 route 独立 send;
     * 任一失败 → 该 route 的 outbox entry 独立存,drain 时只重发失败的。
     */
    TVector<SActorRoute> FindAllActorRoutes(uint64 ActorId) const;

    /**
     * @brief IsActorLocal - 判断该 actor 是否在本进程。
     * @param ActorId 全局 actor id
     * @return true 路由存在且 ServerType == Unknown
     *
     * @detail 只看首条路由,因此 1:N 场景下"任一条为本地"不会让本函数返回 true。
     */
    bool IsActorLocal(uint64 ActorId) const;

    /**
     * @brief LocateActor - 查询 actor 位置。
     * @param ActorId 全局 actor id
     * @return 恒为 Ok 的 future;结果在 FLocateActorResult.bFound / ServerType / ConnectionId
     *   中体现(查不到不是 error,而是 bFound == false)
     *
     * @detail 当前是同步实现的壳:结果立刻兑现,不发起任何跨进程查询
     *   (TargetClassName / MethodName 恒为空)。
     */
    SFutureResult<FLocateActorResult> LocateActor(uint64 ActorId) const;

    // === actor 对象表 + Handle 路径 ===
    // 设计意图:路由(SActorRoute)由 MActorRouter 自己维护,actor 对象(IActor)的存储
    // 委托给 MActorSystem 单例 —— 避免双份所有权/双份锁。
    // 见 Docs/superpowers/specs/2026-08-13-actor-extension-design.md §7.4。
    //
    // 接受 TSharedPtr<IActor>(而非裸指针):所有权一次性转入 MActorSystem,与
    // MActorSystem::Register 语义一致。调用方惯例:
    //   MActorRouter::Get().RegisterActor(NewMObject<MRankListActor>(nullptr, "RankList"), SubPool);
    /**
     * @brief RegisterActor - 注册本地 actor:写一条 ServerType == Unknown 的本地路由并转交 MActorSystem。
     * @param InActor    actor 对象;为空时记 WARN 并返回
     * @param InSubPool  该 actor 归属的 Sub Reactor 池;非空时用 ActorId % GetSubCount() 派生 SubId
     *
     * @detail 路由里的 ConnectionId 存的是 **SubId**(不是连接 id),IsActorLocal 依据
     *   ServerType == Unknown 判定本地性。对象的生命周期所有权在此转交 MActorSystem。
     */
    void RegisterActor(TSharedPtr<IActor> InActor, MSubReactorPool* InSubPool);

    /**
     * @brief UnregisterActorObject - 注销 actor 对象(仅委托 MActorSystem,不动路由表)。
     * @param InActorId 全局 actor id
     */
    void UnregisterActorObject(uint64 InActorId);

    /**
     * @brief FindHandle - 取 actor 句柄,用于从任意线程 Post/Call 该 actor。
     * @param InActorId 全局 actor id
     * @return 句柄;actor 不存在时返回空句柄(由 MActorSystem::Find 决定)
     */
    MActorHandle FindHandle(uint64 InActorId);

    // 模板方式：传类名+函数名
    template <typename TResponse, typename TRequest>
    SFutureResult<TResponse> SendToActor(uint64 ActorId, const char* TargetClassName, const char* FunctionName, const TRequest& Request, EServerType DefaultServerType = EServerType::Unknown) const;

    /**
     * @brief RouteToActor - Lambda 方式:由调用方决定拿到连接后怎么发。
     * @param ActorId 目标 actor id
     * @param Call    形如 (TSharedPtr<MServerConnection>) -> SFutureResult<TResponse> 的可调用体
     * @return Call 的返回值;路由不存在或连接不可用时返回对应的错误 future
     *
     * @detail 适合需要自定义发送逻辑的场景(例如自己拼包);常规调用直接用 SendToActor。
     */
    template <typename TCall> auto RouteToActor(uint64 ActorId, TCall&& Call) const -> decltype(Call(std::declval<TSharedPtr<MServerConnection>>()));

    private:
    // 1:N fan-out:一个 ActorId 对应多条路由。同一个 ActorId 可能在多个 ServerType 上
    // 都有连接(MRankListActor 可能在多台 EchoService 上跑),SendActor 遍历此列表逐个 send。
    TMap<uint64, TVector<SActorRoute>> ActorRoutes; ///< ActorId → 该 actor 的全部路由
    mutable std::mutex                 RoutesMutex; ///< 保护 ActorRoutes
};

// ============================================
// SendToActor 实现
// ============================================

/**
 * @brief SendToActor - 按路由把一次 ServerCall 发到 actor 所在进程。
 * @param ActorId           目标 actor id
 * @param TargetClassName   目标类名
 * @param FunctionName      目标函数名
 * @param Request           请求体
 * @param DefaultServerType 路由表里查不到该 actor 时的兜底服务类型;仍为 Unknown 则直接报错
 * @return 结果 future;失败时是 connection_unavailable / class_not_found / server_call_missing_function
 *
 * @detail 注意:目标连接由 MEndpointCache::GetOrConnect(ServerType) 解析,不看
 *   SActorRoute.ConnectionId —— 即同一 ServerType 上的多个 actor 复用同一条连接。
 */
template <typename TResponse, typename TRequest> SFutureResult<TResponse> MActorRouter::SendToActor(uint64 ActorId, const char* TargetClassName, const char* FunctionName, const TRequest& Request, EServerType DefaultServerType) const {
    SActorRoute Route            = FindActor(ActorId);
    EServerType TargetServerType = Route.ActorId ? Route.ServerType : DefaultServerType;

    TSharedPtr<MServerConnection> Connection = MEndpointCache::Get().GetOrConnect(TargetServerType);
    if (!Connection || !Connection->IsConnected()) {
        return MakeRpcErrorFuture<TResponse>("connection_unavailable", TargetServerType == EServerType::Unknown ? "actor_route_invalid" : "");
    }

    const MClass* TargetClass = MObject::FindClass(TargetClassName);
    if (!TargetClass) {
        return MakeRpcErrorFuture<TResponse>("class_not_found", TargetClassName);
    }

    return CallServerFunction<TResponse>(Connection, TargetClass, FunctionName, Request);
}

// ============================================
// RouteToActor 实现
// ============================================

template <typename TCall> auto MActorRouter::RouteToActor(uint64 ActorId, TCall&& Call) const -> decltype(Call(std::declval<TSharedPtr<MServerConnection>>())) {
    using TResponse = decltype(Call(std::declval<TSharedPtr<MServerConnection>>()));

    SActorRoute Route = FindActor(ActorId);
    if (!Route.ActorId) {
        return MakeRpcErrorFuture<TResponse>("actor_not_found", "Actor not registered");
    }

    TSharedPtr<MServerConnection> Connection = MEndpointCache::Get().GetOrConnect(Route.ServerType);
    if (!Connection || !Connection->IsConnected()) {
        return MakeRpcErrorFuture<TResponse>("connection_unavailable", Route.ServerType == EServerType::Unknown ? "actor_route_invalid" : "");
    }

    return Call(Connection);
}

// ============================================
// 便捷宏：避免手写字符串
// ============================================

#define SERVER_CALL_METHOD(Class, Method) #Class, #Method