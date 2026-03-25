#include "Servers/Gateway/Services/GatewayClientServiceEndpoint.h"

namespace
{
template<typename TResponse>
TResponse BuildClientFailureResponse(const FAppError& Error, const char* FallbackCode)
{
    TResponse Failed;
    Failed.Error = Error.Code.empty() ? (FallbackCode ? FallbackCode : "client_call_failed") : Error.Code;
    return Failed;
}
}

void MGatewayClientServiceEndpoint::Initialize(MGatewayLoginRpc* InLoginRpc, MGatewayWorldRpc* InWorldRpc)
{
    LoginRpc = InLoginRpc;
    WorldRpc = InWorldRpc;
}

void MGatewayClientServiceEndpoint::Client_Echo(FClientEchoRequest& Request, FClientEchoResponse& Response)
{
    Response.ConnectionId = GetCurrentClientConnectionId();
    Response.Message = Request.Message;
}

void MGatewayClientServiceEndpoint::Client_Login(FClientLoginRequest& Request, FClientLoginResponse& Response)
{
    const uint64 GatewayConnectionId = GetCurrentClientConnectionId();
    if (GatewayConnectionId == 0)
    {
        Response.Error = "client_context_missing";
        return;
    }

    const SClientCallContext Context = CaptureCurrentClientCallContext();
    if (!Context.IsValid())
    {
        Response.Error = "client_call_context_missing";
        return;
    }

    (void)MClientCallAsyncSupport::StartDeferred<FClientLoginResponse>(
        Context,
        StartLoginFlow(Request, GatewayConnectionId),
        [](const FAppError& Error)
        {
            return BuildClientFailureResponse<FClientLoginResponse>(Error, "client_login_failed");
        });
}

void MGatewayClientServiceEndpoint::Client_FindPlayer(FClientFindPlayerRequest& Request, FClientFindPlayerResponse& Response)
{
    const SClientCallContext Context = CaptureCurrentClientCallContext();
    if (!Context.IsValid())
    {
        Response.Error = "client_call_context_missing";
        return;
    }

    (void)MClientCallAsyncSupport::StartDeferred<FClientFindPlayerResponse>(
        Context,
        StartFindPlayerFlow(Request),
        [](const FAppError& Error)
        {
            return BuildClientFailureResponse<FClientFindPlayerResponse>(Error, "player_find_failed");
        });
}

void MGatewayClientServiceEndpoint::Client_Logout(FClientLogoutRequest& Request, FClientLogoutResponse& Response)
{
    const SClientCallContext Context = CaptureCurrentClientCallContext();
    if (!Context.IsValid())
    {
        Response.Error = "client_call_context_missing";
        return;
    }

    (void)MClientCallAsyncSupport::StartDeferred<FClientLogoutResponse>(
        Context,
        StartLogoutFlow(Request),
        [](const FAppError& Error)
        {
            return BuildClientFailureResponse<FClientLogoutResponse>(Error, "player_logout_failed");
        });
}

void MGatewayClientServiceEndpoint::Client_SwitchScene(
    FClientSwitchSceneRequest& Request,
    FClientSwitchSceneResponse& Response)
{
    const SClientCallContext Context = CaptureCurrentClientCallContext();
    if (!Context.IsValid())
    {
        Response.Error = "client_call_context_missing";
        return;
    }

    (void)MClientCallAsyncSupport::StartDeferred<FClientSwitchSceneResponse>(
        Context,
        StartSwitchSceneFlow(Request),
        [](const FAppError& Error)
        {
            return BuildClientFailureResponse<FClientSwitchSceneResponse>(Error, "player_switch_scene_failed");
        });
}

MFuture<TResult<FClientLoginResponse, FAppError>> MGatewayClientServiceEndpoint::StartLoginFlow(
    const FClientLoginRequest& Request,
    uint64 GatewayConnectionId) const
{
    if (Request.PlayerId == 0)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FClientLoginResponse>("player_id_required", "Client_Login");
    }

    if (!LoginRpc || !LoginRpc->IsAvailable())
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FClientLoginResponse>("login_server_unavailable", "Client_Login");
    }

    if (!WorldRpc || !WorldRpc->IsAvailable())
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FClientLoginResponse>("world_server_unavailable", "Client_Login");
    }

    FLoginIssueSessionRequest IssueRequest;
    IssueRequest.PlayerId = Request.PlayerId;
    IssueRequest.GatewayConnectionId = GatewayConnectionId;

    return MServerCallAsyncSupport::Chain(
        LoginRpc->IssueSession(IssueRequest),
        [this, PlayerId = Request.PlayerId, GatewayConnectionId](const FLoginIssueSessionResponse& LoginResponse)
        {
            FPlayerEnterWorldRequest EnterRequest;
            EnterRequest.PlayerId = PlayerId;
            EnterRequest.GatewayConnectionId = GatewayConnectionId;
            EnterRequest.SessionKey = LoginResponse.SessionKey;

            return MServerCallAsyncSupport::Map(
                WorldRpc->PlayerEnterWorld(EnterRequest),
                [PlayerId, SessionKey = LoginResponse.SessionKey](const FPlayerEnterWorldResponse&)
                {
                    FClientLoginResponse Response;
                    Response.bSuccess = true;
                    Response.PlayerId = PlayerId;
                    Response.SessionKey = SessionKey;
                    return Response;
                });
        });
}

MFuture<TResult<FClientFindPlayerResponse, FAppError>> MGatewayClientServiceEndpoint::StartFindPlayerFlow(
    const FClientFindPlayerRequest& Request) const
{
    if (Request.PlayerId == 0)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FClientFindPlayerResponse>("player_id_required", "Client_FindPlayer");
    }

    if (!WorldRpc || !WorldRpc->IsAvailable())
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FClientFindPlayerResponse>("world_server_unavailable", "Client_FindPlayer");
    }

    FPlayerFindRequest FindRequest;
    FindRequest.PlayerId = Request.PlayerId;

    return MServerCallAsyncSupport::Map(
        WorldRpc->PlayerFind(FindRequest),
        [](const FPlayerFindResponse& FindValue)
        {
            FClientFindPlayerResponse Response;
            Response.bFound = FindValue.bFound;
            Response.PlayerId = FindValue.PlayerId;
            Response.GatewayConnectionId = FindValue.GatewayConnectionId;
            Response.SceneId = FindValue.SceneId;
            return Response;
        });
}

MFuture<TResult<FClientLogoutResponse, FAppError>> MGatewayClientServiceEndpoint::StartLogoutFlow(
    const FClientLogoutRequest& Request) const
{
    if (Request.PlayerId == 0)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FClientLogoutResponse>("player_id_required", "Client_Logout");
    }

    if (!WorldRpc || !WorldRpc->IsAvailable())
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FClientLogoutResponse>("world_server_unavailable", "Client_Logout");
    }

    FPlayerLogoutRequest LogoutRequest;
    LogoutRequest.PlayerId = Request.PlayerId;

    return MServerCallAsyncSupport::Map(
        WorldRpc->PlayerLogout(LogoutRequest),
        [](const FPlayerLogoutResponse& LogoutValue)
        {
            FClientLogoutResponse Response;
            Response.bSuccess = true;
            Response.PlayerId = LogoutValue.PlayerId;
            return Response;
        });
}

MFuture<TResult<FClientSwitchSceneResponse, FAppError>> MGatewayClientServiceEndpoint::StartSwitchSceneFlow(
    const FClientSwitchSceneRequest& Request) const
{
    if (Request.PlayerId == 0)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FClientSwitchSceneResponse>("player_id_required", "Client_SwitchScene");
    }

    if (!WorldRpc || !WorldRpc->IsAvailable())
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FClientSwitchSceneResponse>("world_server_unavailable", "Client_SwitchScene");
    }

    FPlayerSwitchSceneRequest SwitchRequest;
    SwitchRequest.PlayerId = Request.PlayerId;
    SwitchRequest.SceneId = Request.SceneId;

    return MServerCallAsyncSupport::Chain(
        WorldRpc->PlayerSwitchScene(SwitchRequest),
        [this, PlayerId = Request.PlayerId, SceneId = Request.SceneId](const FPlayerSwitchSceneResponse& SwitchValue)
        {
            FPlayerUpdateRouteRequest RouteRequest;
            RouteRequest.PlayerId = PlayerId;
            RouteRequest.TargetServerType = static_cast<uint8>(EServerType::Scene);
            RouteRequest.SceneId = SceneId;

            return MServerCallAsyncSupport::Map(
                WorldRpc->PlayerUpdateRoute(RouteRequest),
                [SwitchValue](const FPlayerUpdateRouteResponse&)
                {
                    FClientSwitchSceneResponse Response;
                    Response.bSuccess = true;
                    Response.PlayerId = SwitchValue.PlayerId;
                    Response.SceneId = SwitchValue.SceneId;
                    return Response;
                });
        });
}
