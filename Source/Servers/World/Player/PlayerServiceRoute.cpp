#include "Servers/World/Player/PlayerService.h"

#include "Protocol/Messages/Router/RouterServiceMessages.h"
#include "Servers/World/Player/PlayerController.h"

namespace
{
FRouterUpsertPlayerRouteRequest BuildSceneRouteRequest(uint64 PlayerId, uint32 SceneId)
{
    FRouterUpsertPlayerRouteRequest RouteRequest;
    RouteRequest.PlayerId = PlayerId;
    RouteRequest.TargetServerType = static_cast<uint8>(EServerType::Scene);
    RouteRequest.SceneId = SceneId;
    return RouteRequest;
}

FPlayerUpdateRouteRequest BuildPlayerSceneRouteUpdateRequest(uint64 PlayerId, uint32 SceneId)
{
    FPlayerUpdateRouteRequest UpdateRouteRequest;
    UpdateRouteRequest.PlayerId = PlayerId;
    UpdateRouteRequest.SceneId = SceneId;
    UpdateRouteRequest.TargetServerType = static_cast<uint8>(EServerType::Scene);
    return UpdateRouteRequest;
}

FSceneEnterRequest BuildSceneEnterRequest(uint64 PlayerId, uint32 SceneId)
{
    FSceneEnterRequest SceneRequest;
    SceneRequest.PlayerId = PlayerId;
    SceneRequest.SceneId = SceneId;
    return SceneRequest;
}

FSceneLeaveRequest BuildSceneLeaveRequest(uint64 PlayerId)
{
    FSceneLeaveRequest LeaveRequest;
    LeaveRequest.PlayerId = PlayerId;
    return LeaveRequest;
}
} // namespace

MFuture<TResult<FPlayerUpdateRouteResponse, FAppError>> MPlayerService::PlayerUpdateRoute(
    const FPlayerUpdateRouteRequest& Request)
{
    return MPlayerServiceDetail::DispatchPlayerComponent<MPlayerController, FPlayerUpdateRouteResponse>(
        this,
        Request,
        &MPlayerService::FindController,
        &MPlayerController::PlayerUpdateRoute,
        "player_controller_missing",
        "PlayerUpdateRoute");
}

MFuture<TResult<FPlayerUpdateRouteResponse, FAppError>> MPlayerService::ApplySceneRouteForPlayer(
    uint64 PlayerId,
    uint32 SceneId) const
{
    if (auto Error = ValidateDependencies<FPlayerUpdateRouteResponse>(
            "ApplySceneRouteForPlayer",
            {
                EDependency::Router,
            });
        Error.has_value())
    {
        return std::move(*Error);
    }

    const FRouterUpsertPlayerRouteRequest RouteRequest = BuildSceneRouteRequest(PlayerId, SceneId);
    return MServerCallAsyncSupport::Chain(
        WorldServer->GetRouter()->UpsertPlayerRoute(RouteRequest),
        [this, PlayerId, SceneId](const FRouterUpsertPlayerRouteResponse&)
        {
            MPlayerController* Controller = FindController(PlayerId);
            if (!Controller)
            {
                return MServerCallAsyncSupport::MakeErrorFuture<FPlayerUpdateRouteResponse>(
                    "player_controller_missing",
                    "ApplySceneRouteForPlayer");
            }

            return Controller->PlayerUpdateRoute(BuildPlayerSceneRouteUpdateRequest(PlayerId, SceneId));
        });
}

MFuture<TResult<FSceneEnterResponse, FAppError>> MPlayerService::EnterSceneForPlayer(
    uint64 PlayerId,
    uint32 SceneId) const
{
    if (auto Error = ValidateDependencies<FSceneEnterResponse>(
            "EnterSceneForPlayer",
            {
                EDependency::Scene,
            });
        Error.has_value())
    {
        return std::move(*Error);
    }

    if (PlayerId == 0)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FSceneEnterResponse>(
            "player_id_required",
            "EnterSceneForPlayer");
    }

    if (SceneId == 0)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FSceneEnterResponse>(
            "scene_id_required",
            "EnterSceneForPlayer");
    }

    return WorldServer->GetScene()->EnterScene(BuildSceneEnterRequest(PlayerId, SceneId));
}

MFuture<TResult<FSceneLeaveResponse, FAppError>> MPlayerService::LeaveSceneForPlayer(
    uint64 PlayerId,
    uint32 CurrentSceneId) const
{
    if (PlayerId == 0)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FSceneLeaveResponse>(
            "player_id_required",
            "LeaveSceneForPlayer");
    }

    if (CurrentSceneId == 0)
    {
        return MServerCallAsyncSupport::MakeSuccessFuture(FSceneLeaveResponse{PlayerId});
    }

    if (auto Error = ValidateDependencies<FSceneLeaveResponse>(
            "LeaveSceneForPlayer",
            {
                EDependency::Scene,
            });
        Error.has_value())
    {
        return std::move(*Error);
    }

    return WorldServer->GetScene()->LeaveScene(BuildSceneLeaveRequest(PlayerId));
}
