#include "Servers/Scene/Services/SceneSessionServiceEndpoint.h"

void MSceneSessionServiceEndpoint::Initialize(TMap<uint64, uint32>* InPlayerScenes)
{
    PlayerScenes = InPlayerScenes;
}

MFuture<TResult<FSceneEnterResponse, FAppError>> MSceneSessionServiceEndpoint::EnterScene(
    const FSceneEnterRequest& Request)
{
    if (!PlayerScenes)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FSceneEnterResponse>("scene_service_not_initialized", "EnterScene");
    }

    return Implementation.EnterScene(*PlayerScenes, Request);
}

MFuture<TResult<FSceneLeaveResponse, FAppError>> MSceneSessionServiceEndpoint::LeaveScene(
    const FSceneLeaveRequest& Request)
{
    if (!PlayerScenes)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FSceneLeaveResponse>("scene_service_not_initialized", "LeaveScene");
    }

    return Implementation.LeaveScene(*PlayerScenes, Request);
}
