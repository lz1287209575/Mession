#include "Servers/Gateway/Rpc/GatewayBackendRpc.h"

MFuture<TResult<FLoginIssueSessionResponse, FAppError>> MGatewayLoginRpc::IssueSession(
    const FLoginIssueSessionRequest& Request)
{
    return CallRemote<FLoginIssueSessionResponse>(__func__, Request);
}

EServerType MGatewayLoginRpc::GetTargetServerType() const
{
    return EServerType::Login;
}

MFuture<TResult<FPlayerEnterWorldResponse, FAppError>> MGatewayWorldRpc::PlayerEnterWorld(
    const FPlayerEnterWorldRequest& Request)
{
    return CallRemote<FPlayerEnterWorldResponse>(__func__, Request);
}

MFuture<TResult<FPlayerFindResponse, FAppError>> MGatewayWorldRpc::PlayerFind(const FPlayerFindRequest& Request)
{
    return CallRemote<FPlayerFindResponse>(__func__, Request);
}

MFuture<TResult<FPlayerLogoutResponse, FAppError>> MGatewayWorldRpc::PlayerLogout(const FPlayerLogoutRequest& Request)
{
    return CallRemote<FPlayerLogoutResponse>(__func__, Request);
}

MFuture<TResult<FPlayerSwitchSceneResponse, FAppError>> MGatewayWorldRpc::PlayerSwitchScene(
    const FPlayerSwitchSceneRequest& Request)
{
    return CallRemote<FPlayerSwitchSceneResponse>(__func__, Request);
}

MFuture<TResult<FPlayerUpdateRouteResponse, FAppError>> MGatewayWorldRpc::PlayerUpdateRoute(
    const FPlayerUpdateRouteRequest& Request)
{
    return CallRemote<FPlayerUpdateRouteResponse>(__func__, Request);
}

EServerType MGatewayWorldRpc::GetTargetServerType() const
{
    return EServerType::World;
}
