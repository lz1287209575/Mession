#pragma once

#include "Common/Runtime/Reflect/Reflection.h"
#include "Protocol/Messages/Scene/SceneServiceMessages.h"
#include "Servers/App/SceneSessionService.h"

MCLASS(Type=Service)
class MSceneSessionServiceEndpoint : public MObject
{
public:
    MGENERATED_BODY(MSceneSessionServiceEndpoint, MObject, 0)
public:
    void Initialize(TMap<uint64, uint32>* InPlayerScenes);

    MFUNCTION(ServerCall)
    MFuture<TResult<FSceneEnterResponse, FAppError>> EnterScene(const FSceneEnterRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FSceneLeaveResponse, FAppError>> LeaveScene(const FSceneLeaveRequest& Request);

private:
    TMap<uint64, uint32>* PlayerScenes = nullptr;
    MSceneSessionService Implementation;
};
