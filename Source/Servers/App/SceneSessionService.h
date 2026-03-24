#pragma once

#include "Common/Runtime/MLib.h"
#include "Protocol/Messages/Scene/SceneServiceMessages.h"
#include "Servers/App/ServerCallAsyncSupport.h"

class MSceneSessionService
{
public:
    MFuture<TResult<FSceneEnterResponse, FAppError>> EnterScene(
        TMap<uint64, uint32>& PlayerScenes,
        const FSceneEnterRequest& Request) const
    {
        if (Request.PlayerId == 0 || Request.SceneId == 0)
        {
            return MServerCallAsyncSupport::MakeErrorFuture<FSceneEnterResponse>("invalid_scene_enter_request", "EnterScene");
        }

        PlayerScenes[Request.PlayerId] = Request.SceneId;

        FSceneEnterResponse Response;
        Response.PlayerId = Request.PlayerId;
        Response.SceneId = Request.SceneId;
        return MServerCallAsyncSupport::MakeSuccessFuture(std::move(Response));
    }

    MFuture<TResult<FSceneLeaveResponse, FAppError>> LeaveScene(
        TMap<uint64, uint32>& PlayerScenes,
        const FSceneLeaveRequest& Request) const
    {
        if (Request.PlayerId == 0)
        {
            return MServerCallAsyncSupport::MakeErrorFuture<FSceneLeaveResponse>("player_id_required", "LeaveScene");
        }

        PlayerScenes.erase(Request.PlayerId);

        FSceneLeaveResponse Response;
        Response.PlayerId = Request.PlayerId;
        return MServerCallAsyncSupport::MakeSuccessFuture(std::move(Response));
    }
};
