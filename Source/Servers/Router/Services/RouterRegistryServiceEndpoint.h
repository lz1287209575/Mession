#pragma once

#include "Common/Runtime/Reflect/Reflection.h"
#include "Protocol/Messages/Router/RouterServiceMessages.h"
#include "Servers/App/RouterRegistryService.h"

MCLASS(Type=Service)
class MRouterRegistryServiceEndpoint : public MObject
{
public:
    MGENERATED_BODY(MRouterRegistryServiceEndpoint, MObject, 0)
public:
    void Initialize(TMap<uint64, SPlayerRouteRecord>* InRoutes);

    MFUNCTION(ServerCall)
    MFuture<TResult<FRouterResolvePlayerRouteResponse, FAppError>> ResolvePlayerRoute(const FRouterResolvePlayerRouteRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FRouterUpsertPlayerRouteResponse, FAppError>> UpsertPlayerRoute(const FRouterUpsertPlayerRouteRequest& Request);

private:
    TMap<uint64, SPlayerRouteRecord>* Routes = nullptr;
    MRouterRegistryService Implementation;
};
