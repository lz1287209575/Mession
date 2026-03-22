#pragma once

#include "Common/Runtime/MLib.h"
#include "Protocol/Messages/BackendServiceMessages.h"
#include "Servers/App/ServerCallAsyncSupport.h"

struct SPlayerRouteRecord
{
    uint8 TargetServerType = 0;
    uint32 SceneId = 0;
};

class MRouterRegistryService
{
public:
    MFuture<TResult<FRouterResolvePlayerRouteResponse, FAppError>> ResolvePlayerRoute(
        const TMap<uint64, SPlayerRouteRecord>& Routes,
        const FRouterResolvePlayerRouteRequest& Request) const
    {
        if (Request.PlayerId == 0)
        {
            return MServerCallAsyncSupport::MakeErrorFuture<FRouterResolvePlayerRouteResponse>("player_id_required", "ResolvePlayerRoute");
        }

        FRouterResolvePlayerRouteResponse Response;
        Response.PlayerId = Request.PlayerId;
        auto It = Routes.find(Request.PlayerId);
        if (It != Routes.end())
        {
            Response.bFound = true;
            Response.TargetServerType = It->second.TargetServerType;
            Response.SceneId = It->second.SceneId;
        }

        return MServerCallAsyncSupport::MakeSuccessFuture(std::move(Response));
    }

    MFuture<TResult<FRouterUpsertPlayerRouteResponse, FAppError>> UpsertPlayerRoute(
        TMap<uint64, SPlayerRouteRecord>& Routes,
        const FRouterUpsertPlayerRouteRequest& Request) const
    {
        if (Request.PlayerId == 0)
        {
            return MServerCallAsyncSupport::MakeErrorFuture<FRouterUpsertPlayerRouteResponse>("player_id_required", "UpsertPlayerRoute");
        }

        SPlayerRouteRecord& Record = Routes[Request.PlayerId];
        Record.TargetServerType = Request.TargetServerType;
        Record.SceneId = Request.SceneId;

        FRouterUpsertPlayerRouteResponse Response;
        Response.PlayerId = Request.PlayerId;
        return MServerCallAsyncSupport::MakeSuccessFuture(std::move(Response));
    }
};
