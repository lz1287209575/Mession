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

class MActorRouter {
    public:
    static MActorRouter& Get();

    void RegisterActor(uint64 ActorId, EServerType ServerType, uint64 ConnectionId = 0);
    void UnregisterActor(uint64 ActorId);
    void UnregisterRoute(uint64 ActorId, EServerType ServerType, uint64 ConnectionId);
    void UpdateActorRoute(uint64 ActorId, EServerType ServerType, uint64 ConnectionId = 0);

    SActorRoute FindActor(uint64 ActorId) const;

    /**
     * @brief FindAllActorRoutes - 取 actor 全部 route（用于 1:N fan-out）.
     *
     * 一个 ActorId 可对应多个 endpoint（不同 ServerType 各自的连接）—— 这是
     * 1:N fan-out 的基础。SendActor 会遍历此列表,每个 route 独立 send;
     * 任一失败 → 该 route 的 outbox entry 独立存,drain 时只重发失败的。
     */
    TVector<SActorRoute> FindAllActorRoutes(uint64 ActorId) const;

    bool IsActorLocal(uint64 ActorId) const;

    SFutureResult<FLocateActorResult> LocateActor(uint64 ActorId) const;

    // === 阶段 1.2 actor-extension spec:actor 对象表 + Handle 路径 ===
    // 设计意图:MActorRouter 既存路由(SActorRoute)也存 actor 对象(IActor*)。
    // 实际存储 actor 对象的部分委托给 MActorSystem(单例),
    // 避免双份所有权/双份锁。路由表部分仍由 MActorRouter 自己维护。
    // 见 Docs/superpowers/specs/2026-08-13-actor-extension-design.md §7.4。
    //
    // 接受 TSharedPtr<IActor>(而非 IActor*):所有权一次性转入 MActorSystem,
    // 与 MActorSystem::Register 语义一致。调用方惯例:
    //   MActorRouter::Get().RegisterActor(NewMObject<MRankListActor>(nullptr, "RankList"), SubPool);
    void         RegisterActor(TSharedPtr<IActor> InActor, MSubReactorPool* InSubPool);
    void         UnregisterActorObject(uint64 InActorId);
    MActorHandle FindHandle(uint64 InActorId);

    // 模板方式：传类名+函数名
    template <typename TResponse, typename TRequest>
    SFutureResult<TResponse> SendToActor(uint64 ActorId, const char* TargetClassName, const char* FunctionName, const TRequest& Request, EServerType DefaultServerType = EServerType::Unknown) const;

    // Lambda 方式：用户提供如何调用的逻辑
    template <typename TCall> auto RouteToActor(uint64 ActorId, TCall&& Call) const -> decltype(Call(std::declval<TSharedPtr<MServerConnection>>()));

    private:
    // 1:N fan-out:一个 ActorId 对应多个 endpoint(route)。同一个 ActorId 可能在
    // 多个 ServerType 上都有连接(MRankListActor 可能在多台 EchoService 上跑),
    // SendActor 遍历此列表逐个 send。
    TMap<uint64, TVector<SActorRoute>> ActorRoutes;
    mutable std::mutex                 RoutesMutex;
};

// ============================================
// SendToActor 实现
// ============================================

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