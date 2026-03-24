#include "Servers/World/Rpc/WorldBackendRpc.h"

MFuture<TResult<FLoginValidateSessionResponse, FAppError>> MWorldLoginRpc::ValidateSessionCall(
    const FLoginValidateSessionRequest& Request)
{
    return CallRemote<FLoginValidateSessionResponse>(__func__, Request);
}

EServerType MWorldLoginRpc::GetTargetServerType() const
{
    return EServerType::Login;
}

MFuture<TResult<FMgoLoadPlayerResponse, FAppError>> MWorldMgoRpc::LoadPlayer(const FMgoLoadPlayerRequest& Request)
{
    return CallRemote<FMgoLoadPlayerResponse>(__func__, Request);
}

MFuture<TResult<FMgoSavePlayerResponse, FAppError>> MWorldMgoRpc::SavePlayer(const FMgoSavePlayerRequest& Request)
{
    return CallRemote<FMgoSavePlayerResponse>(__func__, Request);
}

EServerType MWorldMgoRpc::GetTargetServerType() const
{
    return EServerType::Mgo;
}

MFuture<TResult<FSceneEnterResponse, FAppError>> MWorldSceneRpc::EnterScene(const FSceneEnterRequest& Request)
{
    return CallRemote<FSceneEnterResponse>(__func__, Request);
}

EServerType MWorldSceneRpc::GetTargetServerType() const
{
    return EServerType::Scene;
}

MFuture<TResult<FRouterUpsertPlayerRouteResponse, FAppError>> MWorldRouterRpc::UpsertPlayerRoute(
    const FRouterUpsertPlayerRouteRequest& Request)
{
    return CallRemote<FRouterUpsertPlayerRouteResponse>(__func__, Request);
}

EServerType MWorldRouterRpc::GetTargetServerType() const
{
    return EServerType::Router;
}
