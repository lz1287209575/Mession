#pragma once

#include "Common/Runtime/Reflect/Reflection.h"
#include "Protocol/Messages/Combat/CombatSceneMessages.h"
#include "Protocol/Messages/Scene/SceneServiceMessages.h"
#include "Servers/App/ServerCallProxy.h"

MCLASS(Type=Rpc)
class MWorldScene : public MServerCallProxyBase
{
public:
    MGENERATED_BODY(MWorldScene, MServerCallProxyBase, 0)
public:
    MFUNCTION(ServerCall, Target=Scene)
    MFuture<TResult<FSceneEnterResponse, FAppError>> EnterScene(const FSceneEnterRequest& Request);

    MFUNCTION(ServerCall, Target=Scene)
    MFuture<TResult<FSceneLeaveResponse, FAppError>> LeaveScene(const FSceneLeaveRequest& Request);

    MFUNCTION(ServerCall, Target=Scene)
    MFuture<TResult<FSceneSpawnCombatAvatarResponse, FAppError>> SpawnCombatAvatar(
        const FSceneSpawnCombatAvatarRequest& Request);

    MFUNCTION(ServerCall, Target=Scene)
    MFuture<TResult<FSceneDespawnCombatAvatarResponse, FAppError>> DespawnCombatAvatar(
        const FSceneDespawnCombatAvatarRequest& Request);

    MFUNCTION(ServerCall, Target=Scene)
    MFuture<TResult<FSceneCastSkillResponse, FAppError>> CastSkill(const FSceneCastSkillRequest& Request);

private:
    EServerType GetTargetServerType() const override;
};

