#pragma once

namespace MWorldPlayerServiceDetail
{
class FPlayerProxyCallBinding
{
public:
    explicit FPlayerProxyCallBinding(const MWorldPlayerServiceEndpoint* InOwner)
        : Owner(InOwner)
    {
    }

    MFuture<TResult<FPlayerFindResponse, FAppError>> PlayerFind(const FPlayerFindRequest& Request) const
    {
        if (Request.PlayerId == 0)
        {
            return MServerCallAsyncSupport::MakeErrorFuture<FPlayerFindResponse>("player_id_required", "PlayerFind");
        }

        if (!Owner || !Owner->OnlinePlayers)
        {
            return MServerCallAsyncSupport::MakeErrorFuture<FPlayerFindResponse>("world_service_not_initialized", "PlayerFind");
        }

        if (!Owner->FindPlayer(Request.PlayerId))
        {
            FPlayerFindResponse Response;
            Response.PlayerId = Request.PlayerId;
            Response.bFound = false;
            return MServerCallAsyncSupport::MakeSuccessFuture(std::move(Response));
        }

        return Owner->DispatchPlayerCall<FPlayerFindResponse>(
            Request.PlayerId,
            "PlayerFind",
            MPlayerProxyCall::EObjectProxyPlayerNode::Root,
            "PlayerFind",
            Request);
    }

#define M_WORLD_PLAYER_PROXY_ROUTE(ServiceMethod, RequestType, ResponseType, NodeName, PlayerFunctionName) \
    MFuture<TResult<ResponseType, FAppError>> ServiceMethod(const RequestType& Request) const \
    { \
        return Dispatch<ResponseType>( \
            Request, \
            MPlayerProxyCall::EObjectProxyPlayerNode::NodeName, \
            PlayerFunctionName); \
    }
#include "Servers/World/Services/WorldPlayerProxyRouteList.inl"
#undef M_WORLD_PLAYER_PROXY_ROUTE

private:
    template<typename TResponse, typename TRequest>
    MFuture<TResult<TResponse, FAppError>> Dispatch(
        const TRequest& Request,
        MPlayerProxyCall::EObjectProxyPlayerNode PlayerNode,
        const char* PlayerFunctionName) const
    {
        if (!Owner)
        {
            return MServerCallAsyncSupport::MakeErrorFuture<TResponse>(
                "world_service_not_initialized",
                PlayerFunctionName ? PlayerFunctionName : "");
        }

        return Owner->DispatchBoundPlayerRequest<TResponse>(
            Request,
            PlayerNode,
            PlayerFunctionName);
    }

private:
    const MWorldPlayerServiceEndpoint* Owner = nullptr;
};
} // namespace MWorldPlayerServiceDetail

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

    return MPlayerProxyCall::Bind(PlayerId, const_cast<MWorldPlayerServiceEndpoint*>(this), PlayerNode)
        .Call<TResponse>(PlayerFunctionName, Request);
}

template<typename TResponse, typename TRequest>
MFuture<TResult<TResponse, FAppError>> MWorldPlayerServiceEndpoint::DispatchBoundPlayerRequest(
    const TRequest& Request,
    MPlayerProxyCall::EObjectProxyPlayerNode PlayerNode,
    const char* PlayerFunctionName,
    const char* ServiceFunctionName) const
{
    const char* ResolvedFunctionName =
        ServiceFunctionName ? ServiceFunctionName : (PlayerFunctionName ? PlayerFunctionName : "");

    if (Request.PlayerId == 0)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<TResponse>(
            "player_id_required",
            ResolvedFunctionName);
    }

    if (!OnlinePlayers)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<TResponse>(
            "world_service_not_initialized",
            ResolvedFunctionName);
    }

    if (!FindPlayer(Request.PlayerId))
    {
        return MServerCallAsyncSupport::MakeErrorFuture<TResponse>("player_not_found", ResolvedFunctionName);
    }

    return DispatchPlayerCall<TResponse>(
        Request.PlayerId,
        ResolvedFunctionName,
        PlayerNode,
        PlayerFunctionName,
        Request);
}

inline MWorldPlayerServiceDetail::FPlayerProxyCallBinding
MWorldPlayerServiceEndpoint::PlayerProxyCall() const
{
    return MWorldPlayerServiceDetail::FPlayerProxyCallBinding(this);
}

template<typename TResponse>
std::optional<MFuture<TResult<TResponse, FAppError>>> MWorldPlayerServiceEndpoint::ValidateDependencies(
    const char* FunctionName,
    std::initializer_list<EWorldPlayerServiceDependency> Dependencies) const
{
    for (const EWorldPlayerServiceDependency Dependency : Dependencies)
    {
        switch (Dependency)
        {
        case EWorldPlayerServiceDependency::OnlinePlayers:
            if (!OnlinePlayers)
            {
                return MServerCallAsyncSupport::MakeErrorFuture<TResponse>(
                    "world_service_not_initialized",
                    FunctionName ? FunctionName : "");
            }
            break;
        case EWorldPlayerServiceDependency::Persistence:
            if (!PersistenceSubsystem)
            {
                return MServerCallAsyncSupport::MakeErrorFuture<TResponse>(
                    "persistence_subsystem_missing",
                    FunctionName ? FunctionName : "");
            }
            break;
        case EWorldPlayerServiceDependency::Login:
            if (!LoginRpc || !LoginRpc->IsAvailable())
            {
                return MServerCallAsyncSupport::MakeErrorFuture<TResponse>(
                    "login_server_unavailable",
                    FunctionName ? FunctionName : "");
            }
            break;
        case EWorldPlayerServiceDependency::Mgo:
            if (!MgoRpc || !MgoRpc->IsAvailable())
            {
                return MServerCallAsyncSupport::MakeErrorFuture<TResponse>(
                    "mgo_server_unavailable",
                    FunctionName ? FunctionName : "");
            }
            break;
        case EWorldPlayerServiceDependency::Scene:
            if (!SceneRpc || !SceneRpc->IsAvailable())
            {
                return MServerCallAsyncSupport::MakeErrorFuture<TResponse>(
                    "scene_server_unavailable",
                    FunctionName ? FunctionName : "");
            }
            break;
        case EWorldPlayerServiceDependency::Router:
            if (!RouterRpc || !RouterRpc->IsAvailable())
            {
                return MServerCallAsyncSupport::MakeErrorFuture<TResponse>(
                    "router_server_unavailable",
                    FunctionName ? FunctionName : "");
            }
            break;
        }
    }

    return std::nullopt;
}
