#include "Servers/World/Player/PlayerService.h"

#include "Servers/World/Player/Player.h"

namespace MPlayerActions
{
class FPlayerSwitchSceneAction final
    : public MServerCallAsyncSupport::TServerCallAction<FPlayerSwitchSceneAction, FPlayerSwitchSceneResponse>
{
public:
    using TResponseType = FPlayerSwitchSceneResponse;

    FPlayerSwitchSceneAction(MPlayerService* InPlayerService, FPlayerSwitchSceneRequest InRequest)
        : PlayerService(InPlayerService)
        , Request(std::move(InRequest))
    {
    }

protected:
    void OnStart() override
    {
        Player = PlayerService->FindPlayer(Request.PlayerId);
        if (!Player)
        {
            Fail("player_not_found", "PlayerSwitchScene");
            return;
        }

        CurrentSceneId = Player->ResolveCurrentSceneId();
        TargetSceneId = Request.SceneId;

        if (CurrentSceneId == TargetSceneId)
        {
            OnSceneEntered(FSceneEnterResponse{Request.PlayerId, TargetSceneId});
            return;
        }

        if (CurrentSceneId != 0)
        {
            Continue(PlayerService->LeaveSceneForPlayer(Request.PlayerId, CurrentSceneId), &FPlayerSwitchSceneAction::OnSceneLeft);
            return;
        }

        EnterTargetScene();
    }

private:
    void OnSceneLeft(const FSceneLeaveResponse&)
    {
        EnterTargetScene();
    }

    void EnterTargetScene()
    {
        Continue(PlayerService->EnterSceneForPlayer(Request.PlayerId, TargetSceneId), &FPlayerSwitchSceneAction::OnSceneEntered);
    }

    void OnSceneEntered(const FSceneEnterResponse& SceneResponse)
    {
        TargetSceneId = SceneResponse.SceneId;
        Continue(
            PlayerService->ApplySceneRouteForPlayer(Request.PlayerId, TargetSceneId),
            &FPlayerSwitchSceneAction::OnRouteApplied);
    }

    void OnRouteApplied(const FPlayerUpdateRouteResponse&)
    {
        if (!Player)
        {
            Fail("player_missing", "PlayerSwitchScene");
            return;
        }

        FPlayerSwitchSceneResponse Response;
        Response.PlayerId = Request.PlayerId;
        Response.SceneId = TargetSceneId;
        Succeed(std::move(Response));
    }

    MPlayerService* PlayerService = nullptr;
    FPlayerSwitchSceneRequest Request;
    MPlayer* Player = nullptr;
    uint32 CurrentSceneId = 0;
    uint32 TargetSceneId = 0;
};
}

MFuture<TResult<FPlayerSwitchSceneResponse, FAppError>> MPlayerService::PlayerSwitchScene(
    const FPlayerSwitchSceneRequest& Request)
{
    if (Request.PlayerId == 0)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerSwitchSceneResponse>("player_id_required", "PlayerSwitchScene");
    }

    if (Request.SceneId == 0)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerSwitchSceneResponse>("scene_id_required", "PlayerSwitchScene");
    }

    if (auto Error = ValidateDependencies<FPlayerSwitchSceneResponse>(
            "PlayerSwitchScene",
            {
                EDependency::Scene,
                EDependency::Router,
            });
        Error.has_value())
    {
        return std::move(*Error);
    }

    uint32 PreviousSceneId = 0;
    if (const MPlayer* Player = FindPlayer(Request.PlayerId))
    {
        PreviousSceneId = Player->ResolveCurrentSceneId();
    }

    MFuture<TResult<FPlayerSwitchSceneResponse, FAppError>> Future =
        MServerCallAsyncSupport::StartAction<MPlayerActions::FPlayerSwitchSceneAction>(this, Request);
    Future.Then([this, PlayerId = Request.PlayerId, PreviousSceneId](MFuture<TResult<FPlayerSwitchSceneResponse, FAppError>> Completed)
    {
        try
        {
            const TResult<FPlayerSwitchSceneResponse, FAppError> Result = Completed.Get();
            if (!Result.IsOk())
            {
                return;
            }

            const uint32 NewSceneId = Result.GetValue().SceneId;
            if (PreviousSceneId != 0 && PreviousSceneId != NewSceneId)
            {
                QueueScenePlayerLeaveNotify(PlayerId, PreviousSceneId);
            }

            QueueScenePlayerEnterNotify(PlayerId);
        }
        catch (...)
        {
        }
    });
    return Future;
}
