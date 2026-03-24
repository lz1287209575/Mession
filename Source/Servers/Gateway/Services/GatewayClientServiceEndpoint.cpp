#include "Servers/Gateway/Services/GatewayClientServiceEndpoint.h"

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

    SGatewayClientFlowDeps FlowDeps;
    FlowDeps.LoginRpc = LoginRpc;
    FlowDeps.WorldRpc = WorldRpc;

    (void)MClientCallAsyncSupport::StartDeferred<FClientLoginResponse>(
        Context,
        MGatewayClientFlows::StartLogin(FlowDeps, Request, GatewayConnectionId),
        [](const FAppError& Error)
        {
            FClientLoginResponse Failed;
            Failed.Error = Error.Code.empty() ? "client_login_failed" : Error.Code;
            return Failed;
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

    SGatewayClientFlowDeps FlowDeps;
    FlowDeps.WorldRpc = WorldRpc;

    (void)MClientCallAsyncSupport::StartDeferred<FClientFindPlayerResponse>(
        Context,
        MGatewayClientFlows::StartFindPlayer(FlowDeps, Request),
        [](const FAppError& Error)
        {
            FClientFindPlayerResponse Failed;
            Failed.Error = Error.Code.empty() ? "player_find_failed" : Error.Code;
            return Failed;
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

    SGatewayClientFlowDeps FlowDeps;
    FlowDeps.WorldRpc = WorldRpc;

    (void)MClientCallAsyncSupport::StartDeferred<FClientLogoutResponse>(
        Context,
        MGatewayClientFlows::StartLogout(FlowDeps, Request),
        [](const FAppError& Error)
        {
            FClientLogoutResponse Failed;
            Failed.Error = Error.Code.empty() ? "player_logout_failed" : Error.Code;
            return Failed;
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

    SGatewayClientFlowDeps FlowDeps;
    FlowDeps.WorldRpc = WorldRpc;

    (void)MClientCallAsyncSupport::StartDeferred<FClientSwitchSceneResponse>(
        Context,
        MGatewayClientFlows::StartSwitchScene(FlowDeps, Request),
        [](const FAppError& Error)
        {
            FClientSwitchSceneResponse Failed;
            Failed.Error = Error.Code.empty() ? "player_switch_scene_failed" : Error.Code;
            return Failed;
        });
}
