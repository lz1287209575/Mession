#pragma once

#include "Common/Net/ServerConnection.h"
#include "Common/Runtime/Object/Object.h"
#include "Common/Runtime/Reflect/Reflection.h"
#include "Protocol/Messages/World/PlayerMovementMessages.h"
#include "Protocol/Messages/World/PlayerRouteMessages.h"
#include "Servers/App/ServerCallAsyncSupport.h"

MCLASS(Type=Object)
class MPlayerController : public MObject
{
public:
    MGENERATED_BODY(MPlayerController, MObject, 0)
public:
    MPlayerController();

    MPROPERTY(PersistentData | Replicated)
    uint32 SceneId = 0;

    MPROPERTY(Replicated)
    uint8 TargetServerType = static_cast<uint8>(EServerType::World);

    void InitializeForLogin(uint32 InSceneId);

    void SetRoute(uint32 InSceneId, uint8 InTargetServerType);

    MFUNCTION(ServerCall)
    MFuture<TResult<FPlayerUpdateRouteResponse, FAppError>> PlayerUpdateRoute(
        const FPlayerUpdateRouteRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FPlayerMoveResponse, FAppError>> PlayerMove(const FPlayerMoveRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FPlayerApplyRouteResponse, FAppError>> ApplyRouteCall(const FPlayerApplyRouteRequest& Request);
};
