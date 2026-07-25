#include "Common/Net/Routing/ActorRouter.h"
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
