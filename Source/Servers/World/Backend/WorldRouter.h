#pragma once

#include "Common/Runtime/Reflect/Reflection.h"
#include "Protocol/Messages/Router/RouterServiceMessages.h"
#include "Servers/App/ServerCallProxy.h"

MCLASS(Type=Rpc)
class MWorldRouter : public MServerCallProxyBase
{
public:
    MGENERATED_BODY(MWorldRouter, MServerCallProxyBase, 0)
public:
    MFUNCTION(ServerCall, Target=Router)
    MFuture<TResult<FRouterUpsertPlayerRouteResponse, FAppError>> UpsertPlayerRoute(const FRouterUpsertPlayerRouteRequest& Request);

private:
    EServerType GetTargetServerType() const override;
};

