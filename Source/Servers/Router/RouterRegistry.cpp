#include "Servers/Router/RouterRegistry.h"

void MRouterRegistry::Initialize(TMap<uint64, SPlayerRouteRecord>* InRoutes)
{
    Routes = InRoutes;
}

MFuture<TResult<FRouterResolvePlayerRouteResponse, FAppError>> MRouterRegistry::ResolvePlayerRoute(
    const FRouterResolvePlayerRouteRequest& Request)
{
    if (!Routes)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FRouterResolvePlayerRouteResponse>(
            "router_service_not_initialized",
            "ResolvePlayerRoute");
    }

    return Implementation.ResolvePlayerRoute(*Routes, Request);
}

MFuture<TResult<FRouterUpsertPlayerRouteResponse, FAppError>> MRouterRegistry::UpsertPlayerRoute(
    const FRouterUpsertPlayerRouteRequest& Request)
{
    if (!Routes)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FRouterUpsertPlayerRouteResponse>(
            "router_service_not_initialized",
            "UpsertPlayerRoute");
    }

    return Implementation.UpsertPlayerRoute(*Routes, Request);
}

