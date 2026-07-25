#pragma once

#include "Common/Net/Routing/ActorRoute.h"
#include "Common/Net/Rpc/RpcServerCall.h"
#include "Common/Net/ServiceDiscovery/EndpointCache.h"
#include "Common/Runtime/Async/MAsync.h"
#include "Common/Runtime/Reflect/Reflection.h"

#include <mutex>

class MActorRouter
{
public:
    static MActorRouter& Get();

    void RegisterActor(uint64 ActorId, EServerType ServerType, uint64 ConnectionId = 0);
    void UnregisterActor(uint64 ActorId);
    void UpdateActorRoute(uint64 ActorId, EServerType ServerType, uint64 ConnectionId = 0);

    SActorRoute FindActor(uint64 ActorId) const;
    bool IsActorLocal(uint64 ActorId) const;

    SFutureResult<FLocateActorResult> LocateActor(uint64 ActorId) const;

    // 模板方式：传类名+函数名
    template<typename TResponse, typename TRequest>
    SFutureResult<TResponse> SendToActor(
        uint64 ActorId,
        const char* TargetClassName,
        const char* FunctionName,
        const TRequest& Request,
        EServerType DefaultServerType = EServerType::Unknown) const;

    // Lambda 方式：用户提供如何调用的逻辑
    template<typename TCall>
    auto RouteToActor(uint64 ActorId, TCall&& Call) const
        -> decltype(Call(std::declval<TSharedPtr<MServerConnection>>()));

private:
    TMap<uint64, SActorRoute> ActorRoutes;
    mutable std::mutex RoutesMutex;
};

// ============================================
// SendToActor 实现
// ============================================

template<typename TResponse, typename TRequest>
SFutureResult<TResponse> MActorRouter::SendToActor(
    uint64 ActorId,
    const char* TargetClassName,
    const char* FunctionName,
    const TRequest& Request,
    EServerType DefaultServerType) const
{
    SActorRoute Route = FindActor(ActorId);
    EServerType TargetServerType = Route.ActorId ? Route.ServerType : DefaultServerType;

    TSharedPtr<MServerConnection> Connection = MEndpointCache::Get().GetOrConnect(TargetServerType);
    if (!Connection || !Connection->IsConnected())
    {
        return MakeRpcErrorFuture<TResponse>("connection_unavailable",
            TargetServerType == EServerType::Unknown ? "actor_route_invalid" : "");
    }

    const MClass* TargetClass = MObject::FindClass(TargetClassName);
    if (!TargetClass)
    {
        return MakeRpcErrorFuture<TResponse>("class_not_found", TargetClassName);
    }

    return CallServerFunction<TResponse>(Connection, TargetClass, FunctionName, Request);
}

// ============================================
// RouteToActor 实现
// ============================================

template<typename TCall>
auto MActorRouter::RouteToActor(
    uint64 ActorId,
    TCall&& Call) const -> decltype(Call(std::declval<TSharedPtr<MServerConnection>>()))
{
    using TResponse = decltype(Call(std::declval<TSharedPtr<MServerConnection>>()));

    SActorRoute Route = FindActor(ActorId);
    if (!Route.ActorId)
    {
        return MakeRpcErrorFuture<TResponse>("actor_not_found", "Actor not registered");
    }

    TSharedPtr<MServerConnection> Connection = MEndpointCache::Get().GetOrConnect(Route.ServerType);
    if (!Connection || !Connection->IsConnected())
    {
        return MakeRpcErrorFuture<TResponse>("connection_unavailable", Route.ServerType == EServerType::Unknown ? "actor_route_invalid" : "");
    }

    return Call(Connection);
}

// ============================================
// 便捷宏：避免手写字符串
// ============================================

#define SERVER_CALL_METHOD(Class, Method) #Class, #Method