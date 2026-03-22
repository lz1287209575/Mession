#include "Servers/App/WorldPlayerService.h"
#include "Servers/World/WorldServer.h"

MFuture<TResult<FPlayerEnterWorldResponse, FAppError>> MWorldPlayerService::EnterWorld(
    TMap<uint64, SWorldPlayerState>& OnlinePlayers,
    const FPlayerEnterWorldRequest& Request) const
{
    if (Request.PlayerId == 0)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerEnterWorldResponse>("player_id_required", "PlayerEnterWorld");
    }

    SWorldPlayerState& PlayerState = OnlinePlayers[Request.PlayerId];
    PlayerState.GatewayConnectionId = Request.GatewayConnectionId;
    PlayerState.SessionKey = Request.SessionKey;
    PlayerState.SceneId = 0;
    PlayerState.TargetServerType = static_cast<uint8>(EServerType::World);

    FPlayerEnterWorldResponse Response;
    Response.PlayerId = Request.PlayerId;
    return MServerCallAsyncSupport::MakeSuccessFuture(std::move(Response));
}

MFuture<TResult<FPlayerFindResponse, FAppError>> MWorldPlayerService::FindPlayer(
    const TMap<uint64, SWorldPlayerState>& OnlinePlayers,
    const FPlayerFindRequest& Request) const
{
    if (Request.PlayerId == 0)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerFindResponse>("player_id_required", "PlayerFind");
    }

    FPlayerFindResponse Response;
    Response.PlayerId = Request.PlayerId;
    auto It = OnlinePlayers.find(Request.PlayerId);
    if (It != OnlinePlayers.end())
    {
        Response.bFound = true;
        Response.GatewayConnectionId = It->second.GatewayConnectionId;
        Response.SceneId = It->second.SceneId;
    }

    return MServerCallAsyncSupport::MakeSuccessFuture(std::move(Response));
}

MFuture<TResult<FPlayerUpdateRouteResponse, FAppError>> MWorldPlayerService::UpdateRoute(
    TMap<uint64, SWorldPlayerState>& OnlinePlayers,
    const FPlayerUpdateRouteRequest& Request) const
{
    if (Request.PlayerId == 0)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerUpdateRouteResponse>("player_id_required", "PlayerUpdateRoute");
    }

    auto It = OnlinePlayers.find(Request.PlayerId);
    if (It == OnlinePlayers.end())
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerUpdateRouteResponse>("player_not_found", "PlayerUpdateRoute");
    }

    It->second.TargetServerType = Request.TargetServerType;
    It->second.SceneId = Request.SceneId;

    FPlayerUpdateRouteResponse Response;
    Response.PlayerId = Request.PlayerId;
    return MServerCallAsyncSupport::MakeSuccessFuture(std::move(Response));
}

MFuture<TResult<FPlayerLogoutResponse, FAppError>> MWorldPlayerService::Logout(
    TMap<uint64, SWorldPlayerState>& OnlinePlayers,
    const FPlayerLogoutRequest& Request) const
{
    if (Request.PlayerId == 0)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerLogoutResponse>("player_id_required", "PlayerLogout");
    }

    OnlinePlayers.erase(Request.PlayerId);

    FPlayerLogoutResponse Response;
    Response.PlayerId = Request.PlayerId;
    return MServerCallAsyncSupport::MakeSuccessFuture(std::move(Response));
}

MFuture<TResult<FPlayerSwitchSceneResponse, FAppError>> MWorldPlayerService::SwitchScene(
    TMap<uint64, SWorldPlayerState>& OnlinePlayers,
    const FPlayerSwitchSceneRequest& Request) const
{
    if (Request.PlayerId == 0)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerSwitchSceneResponse>("player_id_required", "PlayerSwitchScene");
    }

    auto It = OnlinePlayers.find(Request.PlayerId);
    if (It == OnlinePlayers.end())
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerSwitchSceneResponse>("player_not_found", "PlayerSwitchScene");
    }

    It->second.SceneId = Request.SceneId;
    It->second.TargetServerType = static_cast<uint8>(EServerType::Scene);

    FPlayerSwitchSceneResponse Response;
    Response.PlayerId = Request.PlayerId;
    Response.SceneId = Request.SceneId;
    return MServerCallAsyncSupport::MakeSuccessFuture(std::move(Response));
}
