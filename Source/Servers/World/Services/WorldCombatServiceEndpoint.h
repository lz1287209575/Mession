#pragma once

#include "Common/Runtime/Reflect/Reflection.h"
#include "Protocol/Messages/Combat/CombatWorldMessages.h"
#include "Servers/App/ServerCallAsyncSupport.h"
#include "Servers/World/Rpc/WorldBackendRpc.h"

class MPlayer;

MCLASS(Type=Service)
class MWorldCombatServiceEndpoint : public MObject
{
public:
    MGENERATED_BODY(MWorldCombatServiceEndpoint, MObject, 0)
public:
    void Initialize(
        TMap<uint64, MPlayer*>* InOnlinePlayers,
        MWorldSceneRpc* InSceneRpc);

    MFUNCTION(ServerCall)
    MFuture<TResult<FWorldCreateCombatAvatarResponse, FAppError>> CreateCombatAvatar(
        const FWorldCreateCombatAvatarRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FWorldCommitCombatResultResponse, FAppError>> CommitCombatResult(
        const FWorldCommitCombatResultRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FWorldCastSkillResponse, FAppError>> CastSkill(
        const FWorldCastSkillRequest& Request);

private:
    MPlayer* FindPlayer(uint64 PlayerId) const;
    MFuture<TResult<FWorldCreateCombatAvatarResponse, FAppError>> EnsureCombatAvatar(
        uint64 PlayerId,
        uint32 SceneId);

    TMap<uint64, MPlayer*>* OnlinePlayers = nullptr;
    MWorldSceneRpc* SceneRpc = nullptr;
};
