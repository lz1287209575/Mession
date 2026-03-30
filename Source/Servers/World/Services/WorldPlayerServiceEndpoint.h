#pragma once

#include "Common/Runtime/Reflect/Reflection.h"
#include "Protocol/Messages/Scene/SceneServiceMessages.h"
#include "Protocol/Messages/World/WorldPlayerMessages.h"
#include "Servers/App/ObjectProxyCall.h"
#include "Servers/App/ServerCallAsyncSupport.h"
#include "Servers/World/Rpc/WorldBackendRpc.h"
#include "Servers/World/Players/PlayerProxyCall.h"

#include <optional>

class MPersistenceSubsystem;
class MPlayer;

namespace MWorldPlayerServiceDetail
{
class FPlayerProxyCallBinding;
}

namespace MWorldPlayerServiceFlows
{
class FPlayerEnterWorldWorkflow;
class FPlayerLogoutWorkflow;
class FPlayerSwitchSceneWorkflow;
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
    MFuture<TResult<FPlayerQueryProfileResponse, FAppError>> PlayerQueryProfile(const FPlayerQueryProfileRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FPlayerQueryInventoryResponse, FAppError>> PlayerQueryInventory(const FPlayerQueryInventoryRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FPlayerQueryProgressionResponse, FAppError>> PlayerQueryProgression(
        const FPlayerQueryProgressionRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FPlayerChangeGoldResponse, FAppError>> PlayerChangeGold(
        const FPlayerChangeGoldRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FPlayerEquipItemResponse, FAppError>> PlayerEquipItem(
        const FPlayerEquipItemRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FPlayerGrantExperienceResponse, FAppError>> PlayerGrantExperience(
        const FPlayerGrantExperienceRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FPlayerModifyHealthResponse, FAppError>> PlayerModifyHealth(
        const FPlayerModifyHealthRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FPlayerLogoutResponse, FAppError>> PlayerLogout(const FPlayerLogoutRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FPlayerSwitchSceneResponse, FAppError>> PlayerSwitchScene(const FPlayerSwitchSceneRequest& Request);

private:
    enum class EWorldPlayerServiceDependency : uint8
    {
        OnlinePlayers,
        Persistence,
        Login,
        Mgo,
        Scene,
        Router,
    };

    friend class MWorldPlayerServiceFlows::FPlayerEnterWorldWorkflow;
    friend class MWorldPlayerServiceFlows::FPlayerLogoutWorkflow;
    friend class MWorldPlayerServiceFlows::FPlayerSwitchSceneWorkflow;
    friend class MWorldPlayerServiceDetail::FPlayerProxyCallBinding;

    template<typename TResponse, typename TRequest>
    MFuture<TResult<TResponse, FAppError>> DispatchPlayerCall(
        uint64 PlayerId,
        const char* ServiceFunctionName,
        MPlayerProxyCall::EObjectProxyPlayerNode PlayerNode,
        const char* PlayerFunctionName,
        const TRequest& Request) const;

    template<typename TResponse, typename TRequest>
    MFuture<TResult<TResponse, FAppError>> DispatchBoundPlayerRequest(
        const TRequest& Request,
        MPlayerProxyCall::EObjectProxyPlayerNode PlayerNode,
        const char* PlayerFunctionName,
        const char* ServiceFunctionName = nullptr) const;

    MWorldPlayerServiceDetail::FPlayerProxyCallBinding PlayerProxyCall() const;

    MFuture<TResult<FPlayerUpdateRouteResponse, FAppError>> ApplySceneRouteForPlayer(
        uint64 PlayerId,
        uint32 SceneId) const;

    MFuture<TResult<FSceneEnterResponse, FAppError>> EnterSceneForPlayer(
        uint64 PlayerId,
        uint32 SceneId) const;

    MFuture<TResult<FSceneLeaveResponse, FAppError>> LeaveSceneForPlayer(
        uint64 PlayerId,
        uint32 CurrentSceneId) const;

    template<typename TResponse>
    std::optional<MFuture<TResult<TResponse, FAppError>>> ValidateDependencies(
        const char* FunctionName,
        std::initializer_list<EWorldPlayerServiceDependency> Dependencies) const;

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

#include "Servers/World/Services/WorldPlayerServiceEndpoint.inl"
