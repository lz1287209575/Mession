#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Reflect/Reflection.h"
#include "Common/IO/Socket/Socket.h"
#include "Common/Net/NetServerBase.h"
#include "Common/Net/Rpc/RpcRuntimeContext.h"
#include "Common/Net/ServerConnection.h"
#include "Common/Runtime/Log/Logger.h"
#include "Protocol/Messages/Common/AppMessages.h"
#include "Protocol/Messages/Combat/CombatSceneMessages.h"
#include "Protocol/Messages/Scene/SceneServiceMessages.h"
#include "Servers/Scene/Combat/MonsterManager.h"
#include "Servers/Scene/Combat/SceneCombatRuntime.h"
#include "Servers/Scene/Combat/SkillCatalog.h"
#include "Servers/Scene/SceneCombat.h"
#include "Servers/Scene/SceneSession.h"

struct SSceneConfig
{
    uint16 ListenPort = 8004;
    MString SkillAssetDir = "Config/Skills";
};

MCLASS(Type=Server)
class MSceneServer : public MNetServerBase, public MObject, public MServerRuntimeContext
{
public:
    MGENERATED_BODY(MSceneServer, MObject, 0)
public:
    using MObject::Tick;

    bool LoadConfig(const MString& ConfigPath);
    bool Init(int InPort = 0);
    void Tick();
    void Run() override { MNetServerBase::Run(); }

    uint16 GetListenPort() const override;
    void OnAccept(uint64 ConnId, TSharedPtr<INetConnection> Conn) override;
    void ShutdownConnections() override;
    void OnRunStarted() override;

    MFUNCTION(ServerCall)
    MFuture<TResult<FSceneEnterResponse, FAppError>> EnterScene(const FSceneEnterRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FSceneLeaveResponse, FAppError>> LeaveScene(const FSceneLeaveRequest& Request);

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
    MFuture<TResult<FSceneCastSkillResponse, FAppError>> CastSkill(const FSceneCastSkillRequest& Request);

private:
    void HandlePeerPacket(uint64 ConnectionId, const TSharedPtr<INetConnection>& Connection, const TByteArray& Data);
    SSceneConfig Config;
    MSkillCatalog SkillCatalog;
    MSceneCombatRuntime CombatRuntime;
    TMap<uint64, uint32> PlayerScenes;
    TMap<uint64, TSharedPtr<INetConnection>> PeerConnections;
    MSceneSession* Session = nullptr;
    MSceneCombat* Combat = nullptr;
    MMonsterManager* MonsterManager = nullptr;
};
