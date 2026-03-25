#pragma once

#include "Common/Runtime/Reflect/Reflection.h"
#include "Protocol/Messages/Auth/AuthSessionMessages.h"
#include "Protocol/Messages/Mgo/MgoPlayerStateMessages.h"
#include "Protocol/Messages/Router/RouterServiceMessages.h"
#include "Protocol/Messages/Scene/SceneServiceMessages.h"
#include "Protocol/Messages/World/WorldPlayerMessages.h"
#include "Common/Runtime/Persistence/PersistenceSubsystem.h"
#include "Protocol/Messages/Common/ObjectProxyMessages.h"
#include "Servers/App/ObjectProxyCall.h"
#include "Servers/App/ServerCallAsyncSupport.h"
#include "Servers/World/Players/Player.h"
#include "Servers/World/Rpc/WorldBackendRpc.h"

namespace MWorldPlayerServiceFlows
{
class FPlayerEnterWorldWorkflow;
class FPlayerLogoutWorkflow;
}

MCLASS(Type=Service)
class MWorldPlayerServiceEndpoint : public MObject
{
public:
    MGENERATED_BODY(MWorldPlayerServiceEndpoint, MObject, 0)
public:
    void Initialize(
        TMap<uint64, MPlayer*>* InOnlinePlayers,
        MPersistenceSubsystem* InPersistenceSubsystem,
        MWorldLoginRpc* InLoginRpc,
        MWorldMgoRpc* InMgoRpc,
        MWorldSceneRpc* InSceneRpc,
        MWorldRouterRpc* InRouterRpc);

    MFUNCTION(ServerCall)
    MFuture<TResult<FPlayerEnterWorldResponse, FAppError>> PlayerEnterWorld(const FPlayerEnterWorldRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FPlayerFindResponse, FAppError>> PlayerFind(const FPlayerFindRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FPlayerUpdateRouteResponse, FAppError>> PlayerUpdateRoute(const FPlayerUpdateRouteRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FPlayerLogoutResponse, FAppError>> PlayerLogout(const FPlayerLogoutRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FPlayerSwitchSceneResponse, FAppError>> PlayerSwitchScene(const FPlayerSwitchSceneRequest& Request);

private:
    friend class MWorldPlayerServiceFlows::FPlayerEnterWorldWorkflow;
    friend class MWorldPlayerServiceFlows::FPlayerLogoutWorkflow;

    template<typename TResponse, typename TRequest>
    MFuture<TResult<TResponse, FAppError>> DispatchPlayerCall(
        uint64 PlayerId,
        const char* ServiceFunctionName,
        const char* PlayerFunctionName,
        const TRequest& Request) const;

    MPlayer* FindPlayer(uint64 PlayerId) const;
    MPlayer* FindOrCreatePlayer(uint64 PlayerId);
    void RemovePlayer(uint64 PlayerId);

    TMap<uint64, MPlayer*>* OnlinePlayers = nullptr;
    MPersistenceSubsystem* PersistenceSubsystem = nullptr;
    MWorldLoginRpc* LoginRpc = nullptr;
    MWorldMgoRpc* MgoRpc = nullptr;
    MWorldSceneRpc* SceneRpc = nullptr;
    MWorldRouterRpc* RouterRpc = nullptr;
};

template<typename TResponse, typename TRequest>
MFuture<TResult<TResponse, FAppError>> MWorldPlayerServiceEndpoint::DispatchPlayerCall(
    uint64 PlayerId,
    const char* ServiceFunctionName,
    const char* PlayerFunctionName,
    const TRequest& Request) const
{
    if (PlayerId == 0)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<TResponse>("player_id_required", ServiceFunctionName ? ServiceFunctionName : "");
    }

    if (!OnlinePlayers)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<TResponse>(
            "world_service_not_initialized",
            ServiceFunctionName ? ServiceFunctionName : "");
    }

    FObjectProxyTarget Target;
    Target.RootType = EObjectProxyRootType::Player;
    Target.RootId = PlayerId;
    Target.TargetServerType = EServerType::World;
    return MObjectProxyCall::Call<TResponse>(Target, PlayerFunctionName, Request, const_cast<MWorldPlayerServiceEndpoint*>(this));
}
