#include "Servers/World/Player/PlayerService.h"

#include "Common/Runtime/Concurrency/FiberAwait.h"
#include "Servers/World/Player/Player.h"

namespace
{
TResult<FPlayerSwitchSceneResponse, FAppError> MakePlayerSwitchSceneError(const char* Code, const char* Message = "")
{
    return MakeErrorResult<FPlayerSwitchSceneResponse>(FAppError::Make(
        Code ? Code : "player_command_failed",
        Message ? Message : ""));
}
}

MFuture<TResult<FPlayerSwitchSceneResponse, FAppError>> MPlayerService::PlayerSwitchScene(
    const FPlayerSwitchSceneRequest& Request)
{
    return DispatchRuntimeCommand<FPlayerSwitchSceneResponse>(
        Request,
        "PlayerSwitchScene",
        {
            EDependency::Scene,
            EDependency::Router,
        },
        &MPlayerService::DoPlayerSwitchScene);
}

TResult<FPlayerSwitchSceneResponse, FAppError> MPlayerService::DoPlayerSwitchScene(FPlayerSwitchSceneRequest Request)
{
    MPlayer* Player = FindPlayer(Request.PlayerId);
    if (!Player)
    {
        return MakePlayerSwitchSceneError("player_not_found", "PlayerSwitchScene");
    }

    const uint32 PreviousSceneId = Player->ResolveCurrentSceneId();
    uint32 TargetSceneId = Request.SceneId;

    if (PreviousSceneId != TargetSceneId)
    {
        if (PreviousSceneId != 0)
        {
            (void)MAwaitOk(LeaveSceneForPlayer(Request.PlayerId, PreviousSceneId));
        }

        const FSceneEnterResponse SceneResponse =
            MAwaitOk(EnterSceneForPlayer(Request.PlayerId, TargetSceneId));
        TargetSceneId = SceneResponse.SceneId;
    }

    if (const TResult<FPlayerUpdateRouteResponse, FAppError> RouteResult =
            ApplySceneRouteForPlayer(Request.PlayerId, TargetSceneId);
        !RouteResult.IsOk())
    {
        if (PreviousSceneId != TargetSceneId)
        {
            (void)MAwait(LeaveSceneForPlayer(Request.PlayerId, TargetSceneId));
            if (PreviousSceneId != 0)
            {
                (void)MAwait(EnterSceneForPlayer(Request.PlayerId, PreviousSceneId));
            }
        }
        return MakeErrorResult<FPlayerSwitchSceneResponse>(RouteResult.GetError());
    }

    Player = FindPlayer(Request.PlayerId);
    if (!Player)
    {
        return MakePlayerSwitchSceneError("player_missing", "PlayerSwitchScene");
    }

    Player->SyncRuntimeStateToProfile();

    if (PreviousSceneId != 0 && PreviousSceneId != TargetSceneId)
    {
        QueueScenePlayerLeaveNotify(Request.PlayerId, PreviousSceneId);
    }
    QueueScenePlayerEnterNotify(Request.PlayerId);

    FPlayerSwitchSceneResponse Response;
    Response.PlayerId = Request.PlayerId;
    Response.SceneId = TargetSceneId;
    return TResult<FPlayerSwitchSceneResponse, FAppError>::Ok(std::move(Response));
}
