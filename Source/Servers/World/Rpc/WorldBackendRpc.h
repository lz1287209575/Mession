#pragma once

#include "Common/Runtime/Reflect/Reflection.h"
#include "Protocol/Messages/Auth/AuthSessionMessages.h"
#include "Protocol/Messages/Mgo/MgoPlayerStateMessages.h"
#include "Protocol/Messages/Router/RouterServiceMessages.h"
#include "Protocol/Messages/Scene/SceneServiceMessages.h"
#include "Servers/App/ServerCallProxy.h"

MCLASS(Type=Rpc)
class MWorldLoginRpc : public MServerCallProxyBase
{
public:
    MGENERATED_BODY(MWorldLoginRpc, MServerCallProxyBase, 0)
public:
    MFUNCTION(ServerCall, Target=Login)
    MFuture<TResult<FLoginValidateSessionResponse, FAppError>> ValidateSessionCall(const FLoginValidateSessionRequest& Request);

private:
    EServerType GetTargetServerType() const override;
};

MCLASS(Type=Rpc)
class MWorldMgoRpc : public MServerCallProxyBase
{
public:
    MGENERATED_BODY(MWorldMgoRpc, MServerCallProxyBase, 0)
public:
    MFUNCTION(ServerCall, Target=Mgo)
    MFuture<TResult<FMgoLoadPlayerResponse, FAppError>> LoadPlayer(const FMgoLoadPlayerRequest& Request);

    MFUNCTION(ServerCall, Target=Mgo)
    MFuture<TResult<FMgoSavePlayerResponse, FAppError>> SavePlayer(const FMgoSavePlayerRequest& Request);

private:
    EServerType GetTargetServerType() const override;
};

MCLASS(Type=Rpc)
class MWorldSceneRpc : public MServerCallProxyBase
{
public:
    MGENERATED_BODY(MWorldSceneRpc, MServerCallProxyBase, 0)
public:
    MFUNCTION(ServerCall, Target=Scene)
    MFuture<TResult<FSceneEnterResponse, FAppError>> EnterScene(const FSceneEnterRequest& Request);

private:
    EServerType GetTargetServerType() const override;
};

MCLASS(Type=Rpc)
class MWorldRouterRpc : public MServerCallProxyBase
{
public:
    MGENERATED_BODY(MWorldRouterRpc, MServerCallProxyBase, 0)
public:
    MFUNCTION(ServerCall, Target=Router)
    MFuture<TResult<FRouterUpsertPlayerRouteResponse, FAppError>> UpsertPlayerRoute(const FRouterUpsertPlayerRouteRequest& Request);

private:
    EServerType GetTargetServerType() const override;
};
