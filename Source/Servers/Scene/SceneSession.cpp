#include "Servers/Scene/SceneSession.h"

void MSceneSession::Initialize(TMap<uint64, uint32>* InPlayerScenes)
{
    PlayerScenes = InPlayerScenes;
}

MFuture<TResult<FSceneEnterResponse, FAppError>> MSceneSession::EnterScene(
    const FSceneEnterRequest& Request)
{
    if (!PlayerScenes)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FSceneEnterResponse>("scene_service_not_initialized", "EnterScene");
    }

    return Implementation.EnterScene(*PlayerScenes, Request);
}

MFuture<TResult<FSceneLeaveResponse, FAppError>> MSceneSession::LeaveScene(
    const FSceneLeaveRequest& Request)
{
    if (!PlayerScenes)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FSceneLeaveResponse>("scene_service_not_initialized", "LeaveScene");
    }

    return Implementation.LeaveScene(*PlayerScenes, Request);
}

