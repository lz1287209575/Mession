#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/IO/Socket/Socket.h"
#include "Common/Net/NetServerBase.h"
#include "Common/Net/ServerConnection.h"
#include "Common/Net/ServerRpcRuntime.h"
#include "Common/Runtime/Concurrency/Promise.h"
#include "Common/Runtime/Log/Logger.h"
#include "Common/Runtime/Object/Result.h"
#include "Protocol/Messages/AppMessages.h"
#include "Protocol/Messages/WorldPlayerMessages.h"
#include "Servers/App/WorldPlayerService.h"

struct SWorldConfig
{
    uint16 ListenPort = 8003;
};

struct SWorldPlayerState
{
    uint64 GatewayConnectionId = 0;
    uint32 SessionKey = 0;
    uint32 SceneId = 0;
    uint8 TargetServerType = static_cast<uint8>(EServerType::World);
};

MCLASS()
class MWorldServer : public MNetServerBase, public MObject
{
public:
    MGENERATED_BODY(MWorldServer, MObject, 0)
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

    MFUNCTION(ServerCall, Target=World)
    MFuture<TResult<FPlayerEnterWorldResponse, FAppError>> PlayerEnterWorld(const FPlayerEnterWorldRequest& Request);

    MFUNCTION(ServerCall, Target=World)
    MFuture<TResult<FPlayerFindResponse, FAppError>> PlayerFind(const FPlayerFindRequest& Request);

    MFUNCTION(ServerCall, Target=World)
    MFuture<TResult<FPlayerUpdateRouteResponse, FAppError>> PlayerUpdateRoute(const FPlayerUpdateRouteRequest& Request);

    MFUNCTION(ServerCall, Target=World)
    MFuture<TResult<FPlayerLogoutResponse, FAppError>> PlayerLogout(const FPlayerLogoutRequest& Request);

    MFUNCTION(ServerCall, Target=World)
    MFuture<TResult<FPlayerSwitchSceneResponse, FAppError>> PlayerSwitchScene(const FPlayerSwitchSceneRequest& Request);

private:
    void HandlePeerPacket(uint64 ConnectionId, const TSharedPtr<INetConnection>& Connection, const TByteArray& Data);
    SWorldConfig Config;
    TMap<uint64, SWorldPlayerState> OnlinePlayers;
    TMap<uint64, TSharedPtr<INetConnection>> PeerConnections;
    MWorldPlayerService PlayerService;
};
