#include "Common/Net/Routing/ActorRouter.h"
#include "Common/Runtime/Actor/IActor.h"
#include "Common/Runtime/Actor/MActorHandle.h"
#include "Common/Runtime/Actor/MActorSystem.h"
#include "Common/Runtime/Concurrency/SubReactorPool.h"
#include "Common/Runtime/Log/Log.h"
#include "Common/Runtime/Time.h"

MActorRouter& MActorRouter::Get()
{
    static MActorRouter Instance;
    return Instance;
}

void MActorRouter::RegisterActor(uint64 ActorId, EServerType ServerType, uint64 ConnectionId)
{
    std::lock_guard<std::mutex> Lock(RoutesMutex);
    SActorRoute& Route = ActorRoutes[ActorId];
    Route.ActorId = ActorId;
    Route.ServerType = ServerType;
    Route.ConnectionId = ConnectionId;
    Route.LastUpdateTime = static_cast<uint64>(MTime::GetTimeSeconds() * 1000);
}

void MActorRouter::UnregisterActor(uint64 ActorId)
{
    std::lock_guard<std::mutex> Lock(RoutesMutex);
    ActorRoutes.erase(ActorId);
}

void MActorRouter::UpdateActorRoute(uint64 ActorId, EServerType ServerType, uint64 ConnectionId)
{
    RegisterActor(ActorId, ServerType, ConnectionId);
}

SActorRoute MActorRouter::FindActor(uint64 ActorId) const
{
    std::lock_guard<std::mutex> Lock(RoutesMutex);
    auto It = ActorRoutes.find(ActorId);
    if (It != ActorRoutes.end())
    {
        return It->second;
    }
    return {};
}

bool MActorRouter::IsActorLocal(uint64 ActorId) const
{
    SActorRoute Route = FindActor(ActorId);
    return Route.ActorId != 0 && Route.ServerType == EServerType::Unknown;
}

SFutureResult<FLocateActorResult> MActorRouter::LocateActor(uint64 ActorId) const
{
    SActorRoute Route = FindActor(ActorId);

    FLocateActorResult Result;
    if (!Route.ActorId)
    {
        Result.bFound = false;
        MPromise<TResult<FLocateActorResult, FAppError>> Promise;
        Promise.SetValue(TResult<FLocateActorResult, FAppError>::Ok(Result));
        return Promise.GetFuture();
    }

    Result.bFound = true;
    Result.ServerType = Route.ServerType;
    Result.ConnectionId = Route.ConnectionId;
    MPromise<TResult<FLocateActorResult, FAppError>> Promise;
    Promise.SetValue(TResult<FLocateActorResult, FAppError>::Ok(Result));
    return Promise.GetFuture();
}

// ============================================================
// 阶段 1.2 — actor-extension spec
// 设计:本地 actor 对象表由 MActorSystem 维护(单例,持 TSharedPtr<IActor>);
// 本类只维护路由表(SActorRoute)+ SubId 派生。新方法委托给 MActorSystem 以避免双份所有权。
// ============================================================

void MActorRouter::RegisterActor(TSharedPtr<IActor> InActor, MSubReactorPool* InSubPool) {
    if (!InActor) {
        LOG_WARN("MActorRouter::RegisterActor called with null actor");
        return;
    }

    const uint64 ActorId = InActor->GetActorId();
    const uint32 SubCount = (InSubPool != nullptr) ? InSubPool->GetSubCount() : 0;
    const uint32 SubId = SubCount > 0 ? static_cast<uint32>(ActorId % SubCount) : 0;

    // 1) 填 SActorRoute:本地 actor 用 ServerType=Unknown 标记,ConnectionId=SubId
    //    —— SendToActor 的 IsActorLocal 分支依据 ServerType==Unknown 判定
    //    (见 ActorRouter.cpp:47-51 IsActorLocal)。
    {
        std::lock_guard<std::mutex> Lock(RoutesMutex);
        SActorRoute& Route = ActorRoutes[ActorId];
        Route.ActorId         = ActorId;
        Route.ServerType      = EServerType::Unknown;
        Route.ConnectionId    = SubId;
        Route.LastUpdateTime  = static_cast<uint64>(MTime::GetTimeSeconds() * 1000);
    }

    // 2) 委托给 MActorSystem:actor 对象存储 + 在 Sub 线程 Post OnCreated
    //    所有权通过 TSharedPtr 一次性转入 MActorSystem,符合 MakeShared 惯例
    //    (CLAUDE.md "Shared pointers":用 MakeShared,接口赋值 TSharedPtr<IBase> = MakeShared<MImpl>(...))。
    MActorSystem::Get().Register(std::move(InActor));
}

void MActorRouter::UnregisterActorObject(uint64 InActorId) {
    MActorSystem::Get().Unregister(InActorId);
}

MActorHandle MActorRouter::FindHandle(uint64 InActorId) {
    return MActorSystem::Get().Find(InActorId);
}
