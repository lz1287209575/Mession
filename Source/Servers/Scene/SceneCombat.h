#pragma once

#include "Common/Runtime/Reflect/Reflection.h"
#include "Protocol/Messages/Combat/CombatSceneMessages.h"
#include "Servers/App/ServerCallAsyncSupport.h"

class MSceneCombatRuntime;

MCLASS(Type=Service)
class MSceneCombat : public MObject
{
public:
    MGENERATED_BODY(MSceneCombat, MObject, 0)
public:
    void Initialize(
        TMap<uint64, uint32>* InPlayerScenes,
        MSceneCombatRuntime* InRuntime);

    MFUNCTION(ServerCall)
    MFuture<TResult<FSceneSpawnCombatAvatarResponse, FAppError>> SpawnCombatAvatar(
        const FSceneSpawnCombatAvatarRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FSceneSpawnMonsterResponse, FAppError>> SpawnMonster(
        const FSceneSpawnMonsterRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FSceneDespawnCombatAvatarResponse, FAppError>> DespawnCombatAvatar(
        const FSceneDespawnCombatAvatarRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FSceneDespawnCombatUnitResponse, FAppError>> DespawnCombatUnit(
        const FSceneDespawnCombatUnitRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FSceneCastSkillResponse, FAppError>> CastSkill(
        const FSceneCastSkillRequest& Request);

private:
    TMap<uint64, uint32>* PlayerScenes = nullptr;
    MSceneCombatRuntime* Runtime = nullptr;
};
