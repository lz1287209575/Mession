#pragma once

#include "Common/Net/ServerConnection.h"
#include "Common/Runtime/Concurrency/Promise.h"
#include "Common/Runtime/Object/Result.h"
#include "Protocol/Messages/Common/AppMessages.h"
#include "Protocol/Messages/Auth/AuthSessionMessages.h"
#include "Protocol/Messages/Common/ClientCallMessages.h"
#include "Protocol/Messages/World/WorldPlayerMessages.h"
#include "Servers/Gateway/Rpc/GatewayBackendRpc.h"
#include "Servers/App/ServerRpcSupport.h"

struct SGatewayClientFlowDeps
{
    MGatewayLoginRpc* LoginRpc = nullptr;
    MGatewayWorldRpc* WorldRpc = nullptr;
};

namespace MGatewayClientFlows
{
template<typename TResponse>
MFuture<TResult<TResponse, FAppError>> MakeErrorFuture(const char* Code, const char* Message = "")
{
    return MServerRpcSupport::MakeReadyFuture(
        MakeErrorResult<TResponse>(FAppError::Make(Code ? Code : "gateway_error", Message ? Message : "")));
}

inline MFuture<TResult<FClientLoginResponse, FAppError>> StartLogin(
    const SGatewayClientFlowDeps& Deps,
    const FClientLoginRequest& Request,
    uint64 GatewayConnectionId)
{
    if (Request.PlayerId == 0)
    {
        return MakeErrorFuture<FClientLoginResponse>("player_id_required", "Client_Login");
    }

    if (!Deps.LoginRpc || !Deps.LoginRpc->IsAvailable())
    {
        return MakeErrorFuture<FClientLoginResponse>("login_server_unavailable", "Client_Login");
    }

    if (!Deps.WorldRpc || !Deps.WorldRpc->IsAvailable())
    {
        return MakeErrorFuture<FClientLoginResponse>("world_server_unavailable", "Client_Login");
    }

    MPromise<TResult<FClientLoginResponse, FAppError>> Promise;
    MFuture<TResult<FClientLoginResponse, FAppError>> Future = Promise.GetFuture();

    FLoginIssueSessionRequest IssueRequest;
    IssueRequest.PlayerId = Request.PlayerId;
    IssueRequest.GatewayConnectionId = GatewayConnectionId;

    Deps.LoginRpc->IssueSession(IssueRequest)
        .Then(
            [Deps, Promise, Request, GatewayConnectionId](MFuture<TResult<FLoginIssueSessionResponse, FAppError>> LoginFuture) mutable
            {
                const TResult<FLoginIssueSessionResponse, FAppError> LoginResult = LoginFuture.Get();
                if (!LoginResult.IsOk())
                {
                    Promise.SetValue(MakeErrorResult<FClientLoginResponse>(LoginResult.GetError()));
                    return;
                }

                FPlayerEnterWorldRequest EnterRequest;
                EnterRequest.PlayerId = Request.PlayerId;
                EnterRequest.GatewayConnectionId = GatewayConnectionId;
                EnterRequest.SessionKey = LoginResult.GetValue().SessionKey;

                Deps.WorldRpc->PlayerEnterWorld(EnterRequest)
                    .Then(
                        [Promise, Request, SessionKey = LoginResult.GetValue().SessionKey](MFuture<TResult<FPlayerEnterWorldResponse, FAppError>> EnterFuture) mutable
                        {
                            const TResult<FPlayerEnterWorldResponse, FAppError> EnterResult = EnterFuture.Get();
                            if (!EnterResult.IsOk())
                            {
                                Promise.SetValue(MakeErrorResult<FClientLoginResponse>(EnterResult.GetError()));
                                return;
                            }

                            FClientLoginResponse Response;
                            Response.bSuccess = true;
                            Response.PlayerId = Request.PlayerId;
                            Response.SessionKey = SessionKey;
                            Promise.SetValue(TResult<FClientLoginResponse, FAppError>::Ok(std::move(Response)));
                        });
            });

    return Future;
}

inline MFuture<TResult<FClientFindPlayerResponse, FAppError>> StartFindPlayer(
    const SGatewayClientFlowDeps& Deps,
    const FClientFindPlayerRequest& Request)
{
    if (Request.PlayerId == 0)
    {
        return MakeErrorFuture<FClientFindPlayerResponse>("player_id_required", "Client_FindPlayer");
    }

    if (!Deps.WorldRpc || !Deps.WorldRpc->IsAvailable())
    {
        return MakeErrorFuture<FClientFindPlayerResponse>("world_server_unavailable", "Client_FindPlayer");
    }

    MPromise<TResult<FClientFindPlayerResponse, FAppError>> Promise;
    MFuture<TResult<FClientFindPlayerResponse, FAppError>> Future = Promise.GetFuture();

    FPlayerFindRequest FindRequest;
    FindRequest.PlayerId = Request.PlayerId;

    Deps.WorldRpc->PlayerFind(FindRequest)
        .Then(
            [Promise](MFuture<TResult<FPlayerFindResponse, FAppError>> FindFuture) mutable
            {
                const TResult<FPlayerFindResponse, FAppError> FindResult = FindFuture.Get();
                if (!FindResult.IsOk())
                {
                    Promise.SetValue(MakeErrorResult<FClientFindPlayerResponse>(FindResult.GetError()));
                    return;
                }

                const FPlayerFindResponse& FindValue = FindResult.GetValue();
                FClientFindPlayerResponse Response;
                Response.bFound = FindValue.bFound;
                Response.PlayerId = FindValue.PlayerId;
                Response.GatewayConnectionId = FindValue.GatewayConnectionId;
                Response.SceneId = FindValue.SceneId;
                Promise.SetValue(TResult<FClientFindPlayerResponse, FAppError>::Ok(std::move(Response)));
            });

    return Future;
}

inline MFuture<TResult<FClientLogoutResponse, FAppError>> StartLogout(
    const SGatewayClientFlowDeps& Deps,
    const FClientLogoutRequest& Request)
{
    if (Request.PlayerId == 0)
    {
        return MakeErrorFuture<FClientLogoutResponse>("player_id_required", "Client_Logout");
    }

    if (!Deps.WorldRpc || !Deps.WorldRpc->IsAvailable())
    {
        return MakeErrorFuture<FClientLogoutResponse>("world_server_unavailable", "Client_Logout");
    }

    MPromise<TResult<FClientLogoutResponse, FAppError>> Promise;
    MFuture<TResult<FClientLogoutResponse, FAppError>> Future = Promise.GetFuture();

    FPlayerLogoutRequest LogoutRequest;
    LogoutRequest.PlayerId = Request.PlayerId;

    Deps.WorldRpc->PlayerLogout(LogoutRequest)
        .Then(
            [Promise](MFuture<TResult<FPlayerLogoutResponse, FAppError>> LogoutFuture) mutable
            {
                const TResult<FPlayerLogoutResponse, FAppError> LogoutResult = LogoutFuture.Get();
                if (!LogoutResult.IsOk())
                {
                    Promise.SetValue(MakeErrorResult<FClientLogoutResponse>(LogoutResult.GetError()));
                    return;
                }

                FClientLogoutResponse Response;
                Response.bSuccess = true;
                Response.PlayerId = LogoutResult.GetValue().PlayerId;
                Promise.SetValue(TResult<FClientLogoutResponse, FAppError>::Ok(std::move(Response)));
            });

    return Future;
}

inline MFuture<TResult<FClientSwitchSceneResponse, FAppError>> StartSwitchScene(
    const SGatewayClientFlowDeps& Deps,
    const FClientSwitchSceneRequest& Request)
{
    if (Request.PlayerId == 0)
    {
        return MakeErrorFuture<FClientSwitchSceneResponse>("player_id_required", "Client_SwitchScene");
    }

    if (!Deps.WorldRpc || !Deps.WorldRpc->IsAvailable())
    {
        return MakeErrorFuture<FClientSwitchSceneResponse>("world_server_unavailable", "Client_SwitchScene");
    }

    MPromise<TResult<FClientSwitchSceneResponse, FAppError>> Promise;
    MFuture<TResult<FClientSwitchSceneResponse, FAppError>> Future = Promise.GetFuture();

    FPlayerSwitchSceneRequest SwitchRequest;
    SwitchRequest.PlayerId = Request.PlayerId;
    SwitchRequest.SceneId = Request.SceneId;

    Deps.WorldRpc->PlayerSwitchScene(SwitchRequest)
        .Then(
            [Deps, Promise, Request](MFuture<TResult<FPlayerSwitchSceneResponse, FAppError>> SwitchFuture) mutable
            {
                const TResult<FPlayerSwitchSceneResponse, FAppError> SwitchResult = SwitchFuture.Get();
                if (!SwitchResult.IsOk())
                {
                    Promise.SetValue(MakeErrorResult<FClientSwitchSceneResponse>(SwitchResult.GetError()));
                    return;
                }

                FPlayerUpdateRouteRequest RouteRequest;
                RouteRequest.PlayerId = Request.PlayerId;
                RouteRequest.TargetServerType = static_cast<uint8>(EServerType::Scene);
                RouteRequest.SceneId = Request.SceneId;

                Deps.WorldRpc->PlayerUpdateRoute(RouteRequest)
                    .Then(
                        [Promise, SwitchValue = SwitchResult.GetValue()](MFuture<TResult<FPlayerUpdateRouteResponse, FAppError>> RouteFuture) mutable
                        {
                            const TResult<FPlayerUpdateRouteResponse, FAppError> RouteResult = RouteFuture.Get();
                            if (!RouteResult.IsOk())
                            {
                                Promise.SetValue(MakeErrorResult<FClientSwitchSceneResponse>(RouteResult.GetError()));
                                return;
                            }

                            FClientSwitchSceneResponse Response;
                            Response.bSuccess = true;
                            Response.PlayerId = SwitchValue.PlayerId;
                            Response.SceneId = SwitchValue.SceneId;
                            Promise.SetValue(TResult<FClientSwitchSceneResponse, FAppError>::Ok(std::move(Response)));
                        });
            });

    return Future;
}
}
