#include "Servers/World/Players/PlayerController.h"

#include "Servers/World/Players/Player.h"

MPlayerController::MPlayerController()
{
}

void MPlayerController::InitializeForLogin(uint32 InSceneId)
{
    SceneId = InSceneId;
    MarkPropertyDirty("SceneId");
}

void MPlayerController::SetRoute(uint32 InSceneId, uint8 InTargetServerType)
{
    SceneId = InSceneId;
    TargetServerType = InTargetServerType;
    MarkPropertyDirty("SceneId");
    MarkPropertyDirty("TargetServerType");
}

MFuture<TResult<FPlayerMoveResponse, FAppError>> MPlayerController::PlayerMove(const FPlayerMoveRequest& Request)
{
    MPlayer* Player = dynamic_cast<MPlayer*>(GetOuter());
    if (!Player)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerMoveResponse>(
            "player_owner_missing",
            "PlayerMove");
    }

    MPlayerPawn* Pawn = Player->GetPawn();
    if (!Pawn)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerMoveResponse>(
            "player_pawn_missing",
            "PlayerMove");
    }

    if (!Pawn->IsSpawned())
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerMoveResponse>(
            "player_pawn_not_spawned",
            "PlayerMove");
    }

    Pawn->SetLocation(Request.X, Request.Y, Request.Z);

    FPlayerMoveResponse Response;
    Response.PlayerId = Player->PlayerId;
    Response.SceneId = Pawn->SceneId;
    Response.X = Pawn->X;
    Response.Y = Pawn->Y;
    Response.Z = Pawn->Z;
    Response.Health = Pawn->Health;
    return MServerCallAsyncSupport::MakeSuccessFuture(std::move(Response));
}

MFuture<TResult<FPlayerUpdateRouteResponse, FAppError>> MPlayerController::PlayerUpdateRoute(
    const FPlayerUpdateRouteRequest& Request)
{
    FPlayerApplyRouteRequest ApplyRequest;
    ApplyRequest.SceneId = Request.SceneId;
    ApplyRequest.TargetServerType = Request.TargetServerType;
    return MServerCallAsyncSupport::Map(
        ApplyRouteCall(ApplyRequest),
        [](const FPlayerApplyRouteResponse& ApplyValue)
        {
            FPlayerUpdateRouteResponse Response;
            Response.PlayerId = ApplyValue.PlayerId;
            return Response;
        });
}

MFuture<TResult<FPlayerApplyRouteResponse, FAppError>> MPlayerController::ApplyRouteCall(
    const FPlayerApplyRouteRequest& Request)
{
    if (Request.SceneId == 0)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerApplyRouteResponse>(
            "scene_id_required",
            "ApplyRouteCall");
    }

    MPlayer* Player = dynamic_cast<MPlayer*>(GetOuter());
    if (!Player)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerApplyRouteResponse>(
            "player_owner_missing",
            "ApplyRouteCall");
    }

    Player->SetRoute(Request.SceneId, Request.TargetServerType);

    FPlayerApplyRouteResponse Response;
    Response.PlayerId = Player->PlayerId;
    Response.SceneId = Request.SceneId;
    Response.TargetServerType = Request.TargetServerType;
    return MServerCallAsyncSupport::MakeSuccessFuture(std::move(Response));
}
