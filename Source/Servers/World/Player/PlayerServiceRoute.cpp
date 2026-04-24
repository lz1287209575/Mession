#include "Servers/World/Player/PlayerService.h"

#include "Common/Runtime/Concurrency/FiberAwait.h"
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
    return DispatchPlayerComponent<MPlayerController, FPlayerUpdateRouteResponse>(
        Request,
        &MPlayerService::FindController,
        &MPlayerController::PlayerUpdateRoute,
        "player_controller_missing",
        "PlayerUpdateRoute");
}

TResult<FPlayerUpdateRouteResponse, FAppError> MPlayerService::ApplySceneRouteForPlayer(
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
        return MAwait(std::move(*Error));
    }

    MPlayerController* Controller = FindController(PlayerId);
    if (!Controller)
    {
        return MakeErrorResult<FPlayerUpdateRouteResponse>(FAppError::Make(
            "player_controller_missing",
            "ApplySceneRouteForPlayer"));
    }

    const uint32 PreviousSceneId = Controller->SceneId;
    const uint8 PreviousTargetServerType = Controller->TargetServerType;

    const FRouterUpsertPlayerRouteRequest RouteRequest = BuildSceneRouteRequest(PlayerId, SceneId);
    (void)MAwaitOk(WorldServer->GetRouter()->UpsertPlayerRoute(RouteRequest));

    const TResult<FPlayerUpdateRouteResponse, FAppError> UpdateResult =
        MAwait(Controller->PlayerUpdateRoute(BuildPlayerSceneRouteUpdateRequest(PlayerId, SceneId)));
    if (UpdateResult.IsOk())
    {
        return UpdateResult;
    }

    if (PreviousSceneId != 0)
    {
        FRouterUpsertPlayerRouteRequest RollbackRequest;
        RollbackRequest.PlayerId = PlayerId;
        RollbackRequest.TargetServerType = PreviousTargetServerType;
        RollbackRequest.SceneId = PreviousSceneId;
        (void)MAwait(WorldServer->GetRouter()->UpsertPlayerRoute(RollbackRequest));
    }

    return UpdateResult;
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
