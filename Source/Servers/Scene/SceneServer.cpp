#include "Servers/Scene/SceneServer.h"
#include "Servers/App/ServerRpcSupport.h"
#include "Common/Runtime/Object/Object.h"

bool MSceneServer::LoadConfig(const MString& /*ConfigPath*/)
{
    return true;
}

bool MSceneServer::Init(int InPort)
{
    if (InPort > 0)
    {
        Config.ListenPort = static_cast<uint16>(InPort);
    }

    bRunning = true;
    MLogger::LogStartupBanner("SceneServer", Config.ListenPort, 0);
    MServerConnection::SetLocalInfo(4, EServerType::Scene, "SceneSkeleton");

    SkillCatalog.LoadBuiltInDefaults();
    TVector<MString> SkillWarnings;
    (void)SkillCatalog.LoadFromDirectory(Config.SkillAssetDir, &SkillWarnings);
    for (const MString& Warning : SkillWarnings)
    {
        LOG_WARN("Scene skill catalog warning: %s", Warning.c_str());
    }

    if (!Session)
    {
        Session = NewMObject<MSceneSession>(this, "Session");
    }
    if (!Combat)
    {
        Combat = NewMObject<MSceneCombat>(this, "Combat");
    }
    if (!MonsterManager)
    {
        MonsterManager = NewMObject<MMonsterManager>(this, "MonsterManager");
    }
    CombatRuntime.Initialize(&SkillCatalog, MonsterManager);
    Session->Initialize(&PlayerScenes);
    Combat->Initialize(&PlayerScenes, &CombatRuntime);
    return true;
}

void MSceneServer::Tick()
{
    CombatRuntime.Tick(DEFAULT_TICK_RATE);
}

uint16 MSceneServer::GetListenPort() const
{
    return Config.ListenPort;
}

void MSceneServer::OnAccept(uint64 ConnId, TSharedPtr<INetConnection> Conn)
{
    PeerConnections[ConnId] = Conn;
    EventLoop.RegisterConnection(
        ConnId,
        Conn,
        [this, Conn](uint64 ConnectionId, const TByteArray& Payload)
        {
            HandlePeerPacket(ConnectionId, Conn, Payload);
        },
        [this](uint64 ConnectionId)
        {
            PeerConnections.erase(ConnectionId);
        });
}

void MSceneServer::ShutdownConnections()
{
    ClearRpcTransports();
    for (auto& [ConnId, Conn] : PeerConnections)
    {
        (void)ConnId;
        if (Conn)
        {
            Conn->Close();
        }
    }
    PeerConnections.clear();
}

void MSceneServer::OnRunStarted()
{
    LOG_INFO("Scene skeleton running on port %u", static_cast<unsigned>(Config.ListenPort));
}

MFuture<TResult<FSceneEnterResponse, FAppError>> MSceneServer::EnterScene(const FSceneEnterRequest& Request)
{
    if (!Session)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FSceneEnterResponse>(
            "scene_service_missing",
            "EnterScene");
    }

    return Session->EnterScene(Request);
}

MFuture<TResult<FSceneLeaveResponse, FAppError>> MSceneServer::LeaveScene(const FSceneLeaveRequest& Request)
{
    if (!Session)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FSceneLeaveResponse>(
            "scene_service_missing",
            "LeaveScene");
    }

    MString IgnoredCombatError;
    (void)CombatRuntime.DespawnAvatar(Request.PlayerId, nullptr, IgnoredCombatError);

    return Session->LeaveScene(Request);
}

MFuture<TResult<FSceneSpawnCombatAvatarResponse, FAppError>> MSceneServer::SpawnCombatAvatar(
    const FSceneSpawnCombatAvatarRequest& Request)
{
    if (!Combat)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FSceneSpawnCombatAvatarResponse>(
            "scene_combat_service_missing",
            "SpawnCombatAvatar");
    }

    return Combat->SpawnCombatAvatar(Request);
}

MFuture<TResult<FSceneSpawnMonsterResponse, FAppError>> MSceneServer::SpawnMonster(
    const FSceneSpawnMonsterRequest& Request)
{
    if (!Combat)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FSceneSpawnMonsterResponse>(
            "scene_combat_service_missing",
            "SpawnMonster");
    }

    return Combat->SpawnMonster(Request);
}

MFuture<TResult<FSceneDespawnCombatAvatarResponse, FAppError>> MSceneServer::DespawnCombatAvatar(
    const FSceneDespawnCombatAvatarRequest& Request)
{
    if (!Combat)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FSceneDespawnCombatAvatarResponse>(
            "scene_combat_service_missing",
            "DespawnCombatAvatar");
    }

    return Combat->DespawnCombatAvatar(Request);
}

MFuture<TResult<FSceneDespawnCombatUnitResponse, FAppError>> MSceneServer::DespawnCombatUnit(
    const FSceneDespawnCombatUnitRequest& Request)
{
    if (!Combat)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FSceneDespawnCombatUnitResponse>(
            "scene_combat_service_missing",
            "DespawnCombatUnit");
    }

    return Combat->DespawnCombatUnit(Request);
}

MFuture<TResult<FSceneCastSkillResponse, FAppError>> MSceneServer::CastSkill(
    const FSceneCastSkillRequest& Request)
{
    if (!Combat)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FSceneCastSkillResponse>(
            "scene_combat_service_missing",
            "CastSkill");
    }

    return Combat->CastSkill(Request);
}

void MSceneServer::HandlePeerPacket(uint64 /*ConnectionId*/, const TSharedPtr<INetConnection>& Connection, const TByteArray& Data)
{
    (void)MServerRpcSupport::DispatchServerCallPacket(this, Connection, Data);
}
