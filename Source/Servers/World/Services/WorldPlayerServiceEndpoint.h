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
#include "Servers/World/Players/PlayerProxyCall.h"
#include "Servers/World/Rpc/WorldBackendRpc.h"

namespace MWorldPlayerServiceFlows
{
class FPlayerEnterWorldWorkflow;
class FPlayerLogoutWorkflow;
class FPlayerSwitchSceneWorkflow;
}

namespace MWorldPlayerServiceDispatch
{
template<typename TRequest>
struct TPlayerRpcBindingTraits;

template<>
struct TPlayerRpcBindingTraits<FPlayerFindRequest>
{
    using TResponse = FPlayerFindResponse;
    static constexpr MPlayerProxyCall::EObjectProxyPlayerNode PlayerNode =
        MPlayerProxyCall::EObjectProxyPlayerNode::Root;

    static const char* FunctionName()
    {
        return "PlayerFind";
    }
};

template<>
struct TPlayerRpcBindingTraits<FPlayerUpdateRouteRequest>
{
    using TResponse = FPlayerUpdateRouteResponse;
    static constexpr MPlayerProxyCall::EObjectProxyPlayerNode PlayerNode =
        MPlayerProxyCall::EObjectProxyPlayerNode::Controller;

    static const char* FunctionName()
    {
        return "PlayerUpdateRoute";
    }
};

template<>
struct TPlayerRpcBindingTraits<FPlayerQueryProfileRequest>
{
    using TResponse = FPlayerQueryProfileResponse;
    static constexpr MPlayerProxyCall::EObjectProxyPlayerNode PlayerNode =
        MPlayerProxyCall::EObjectProxyPlayerNode::Profile;

    static const char* FunctionName()
    {
        return "PlayerQueryProfile";
    }
};

template<>
struct TPlayerRpcBindingTraits<FPlayerQueryInventoryRequest>
{
    using TResponse = FPlayerQueryInventoryResponse;
    static constexpr MPlayerProxyCall::EObjectProxyPlayerNode PlayerNode =
        MPlayerProxyCall::EObjectProxyPlayerNode::Inventory;

    static const char* FunctionName()
    {
        return "PlayerQueryInventory";
    }
};

template<>
struct TPlayerRpcBindingTraits<FPlayerQueryProgressionRequest>
{
    using TResponse = FPlayerQueryProgressionResponse;
    static constexpr MPlayerProxyCall::EObjectProxyPlayerNode PlayerNode =
        MPlayerProxyCall::EObjectProxyPlayerNode::Progression;

    static const char* FunctionName()
    {
        return "PlayerQueryProgression";
    }
};

template<>
struct TPlayerRpcBindingTraits<FPlayerChangeGoldRequest>
{
    using TResponse = FPlayerChangeGoldResponse;
    static constexpr MPlayerProxyCall::EObjectProxyPlayerNode PlayerNode =
        MPlayerProxyCall::EObjectProxyPlayerNode::Inventory;

    static const char* FunctionName()
    {
        return "PlayerChangeGold";
    }
};

template<>
struct TPlayerRpcBindingTraits<FPlayerEquipItemRequest>
{
    using TResponse = FPlayerEquipItemResponse;
    static constexpr MPlayerProxyCall::EObjectProxyPlayerNode PlayerNode =
        MPlayerProxyCall::EObjectProxyPlayerNode::Inventory;

    static const char* FunctionName()
    {
        return "PlayerEquipItem";
    }
};

template<>
struct TPlayerRpcBindingTraits<FPlayerGrantExperienceRequest>
{
    using TResponse = FPlayerGrantExperienceResponse;
    static constexpr MPlayerProxyCall::EObjectProxyPlayerNode PlayerNode =
        MPlayerProxyCall::EObjectProxyPlayerNode::Progression;

    static const char* FunctionName()
    {
        return "PlayerGrantExperience";
    }
};

template<>
struct TPlayerRpcBindingTraits<FPlayerModifyHealthRequest>
{
    using TResponse = FPlayerModifyHealthResponse;
    static constexpr MPlayerProxyCall::EObjectProxyPlayerNode PlayerNode =
        MPlayerProxyCall::EObjectProxyPlayerNode::Progression;

    static const char* FunctionName()
    {
        return "PlayerModifyHealth";
    }
};
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
    friend class MWorldPlayerServiceFlows::FPlayerEnterWorldWorkflow;
    friend class MWorldPlayerServiceFlows::FPlayerLogoutWorkflow;
    friend class MWorldPlayerServiceFlows::FPlayerSwitchSceneWorkflow;

    template<typename TResponse, typename TRequest>
    MFuture<TResult<TResponse, FAppError>> DispatchPlayerCall(
        uint64 PlayerId,
        const char* ServiceFunctionName,
        MPlayerProxyCall::EObjectProxyPlayerNode PlayerNode,
        const char* PlayerFunctionName,
        const TRequest& Request) const;

    template<typename TRequest>
    auto DispatchPlayerRequest(const TRequest& Request) const
        -> MFuture<TResult<typename MWorldPlayerServiceDispatch::TPlayerRpcBindingTraits<TRequest>::TResponse, FAppError>>;

    template<typename TRequest>
    auto DispatchExistingPlayerRequest(const TRequest& Request, const char* ServiceFunctionName) const
        -> MFuture<TResult<typename MWorldPlayerServiceDispatch::TPlayerRpcBindingTraits<TRequest>::TResponse, FAppError>>;

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
    MPlayerProxyCall::EObjectProxyPlayerNode PlayerNode,
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

    return MPlayerProxyCall::Bind(PlayerId, const_cast<MWorldPlayerServiceEndpoint*>(this), PlayerNode)
        .Call<TResponse>(PlayerFunctionName, Request);
}

template<typename TRequest>
auto MWorldPlayerServiceEndpoint::DispatchPlayerRequest(const TRequest& Request) const
    -> MFuture<TResult<typename MWorldPlayerServiceDispatch::TPlayerRpcBindingTraits<TRequest>::TResponse, FAppError>>
{
    using TBinding = MWorldPlayerServiceDispatch::TPlayerRpcBindingTraits<TRequest>;
    return DispatchPlayerCall<typename TBinding::TResponse>(
        Request.PlayerId,
        TBinding::FunctionName(),
        TBinding::PlayerNode,
        TBinding::FunctionName(),
        Request);
}

template<typename TRequest>
auto MWorldPlayerServiceEndpoint::DispatchExistingPlayerRequest(const TRequest& Request, const char* ServiceFunctionName) const
    -> MFuture<TResult<typename MWorldPlayerServiceDispatch::TPlayerRpcBindingTraits<TRequest>::TResponse, FAppError>>
{
    using TResponse = typename MWorldPlayerServiceDispatch::TPlayerRpcBindingTraits<TRequest>::TResponse;

    if (Request.PlayerId == 0)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<TResponse>(
            "player_id_required",
            ServiceFunctionName ? ServiceFunctionName : "");
    }

    if (!OnlinePlayers)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<TResponse>(
            "world_service_not_initialized",
            ServiceFunctionName ? ServiceFunctionName : "");
    }

    if (!FindPlayer(Request.PlayerId))
    {
        return MServerCallAsyncSupport::MakeErrorFuture<TResponse>(
            "player_not_found",
            ServiceFunctionName ? ServiceFunctionName : "");
    }

    return DispatchPlayerRequest(Request);
}
