#include "Servers/World/Services/WorldClientServiceEndpoint.h"

namespace MWorldClientFlows
{
template<typename TResponse>
TResponse BuildClientFailureResponse(const FAppError& Error, const char* FallbackCode)
{
    TResponse Failed;
    Failed.Error = Error.Code.empty() ? (FallbackCode ? FallbackCode : "client_call_failed") : Error.Code;
    return Failed;
}

class FClientLoginWorkflow final
    : public MClientCallAsyncSupport::TClientCallWorkflow<FClientLoginWorkflow, FClientLoginResponse>
{
public:
    using TResponseType = FClientLoginResponse;

    FClientLoginWorkflow(
        MWorldLoginRpc* InLoginRpc,
        MWorldPlayerServiceEndpoint* InPlayerService,
        FClientLoginRequest InRequest,
        uint64 InGatewayConnectionId)
        : LoginRpc(InLoginRpc)
        , PlayerService(InPlayerService)
        , Request(std::move(InRequest))
        , GatewayConnectionId(InGatewayConnectionId)
    {
    }

protected:
    void OnStart() override
    {
        if (!LoginRpc || !PlayerService)
        {
            Fail("client_login_dependencies_missing", "Client_Login");
            return;
        }

        FLoginIssueSessionRequest IssueRequest;
        IssueRequest.PlayerId = Request.PlayerId;
        IssueRequest.GatewayConnectionId = GatewayConnectionId;
        Continue(LoginRpc->IssueSession(IssueRequest), &FClientLoginWorkflow::OnSessionIssued);
    }

private:
    void OnSessionIssued(const FLoginIssueSessionResponse& LoginResponse)
    {
        SessionKey = LoginResponse.SessionKey;

        FPlayerEnterWorldRequest EnterRequest;
        EnterRequest.PlayerId = Request.PlayerId;
        EnterRequest.GatewayConnectionId = GatewayConnectionId;
        EnterRequest.SessionKey = SessionKey;
        Continue(PlayerService->PlayerEnterWorld(EnterRequest), &FClientLoginWorkflow::OnPlayerEnteredWorld);
    }

    void OnPlayerEnteredWorld(const FPlayerEnterWorldResponse&)
    {
        FClientLoginResponse Response;
        Response.bSuccess = true;
        Response.PlayerId = Request.PlayerId;
        Response.SessionKey = SessionKey;
        Succeed(std::move(Response));
    }

    MWorldLoginRpc* LoginRpc = nullptr;
    MWorldPlayerServiceEndpoint* PlayerService = nullptr;
    FClientLoginRequest Request;
    uint64 GatewayConnectionId = 0;
    uint32 SessionKey = 0;
};
}

void MWorldClientServiceEndpoint::Initialize(
    MWorldPlayerServiceEndpoint* InPlayerService,
    MWorldLoginRpc* InLoginRpc)
{
    PlayerService = InPlayerService;
    LoginRpc = InLoginRpc;
}

void MWorldClientServiceEndpoint::Client_Login(FClientLoginRequest& Request, FClientLoginResponse& Response)
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
        StartClientLoginFlow(Request, GatewayConnectionId),
        [](const FAppError& Error)
        {
            return MWorldClientFlows::BuildClientFailureResponse<FClientLoginResponse>(Error, "client_login_failed");
        });
}

void MWorldClientServiceEndpoint::Client_FindPlayer(FClientFindPlayerRequest& Request, FClientFindPlayerResponse& Response)
{
    const SClientCallContext Context = CaptureCurrentClientCallContext();
    if (!Context.IsValid())
    {
        Response.Error = "client_call_context_missing";
        return;
    }

    (void)MClientCallAsyncSupport::StartDeferred<FClientFindPlayerResponse>(
        Context,
        StartClientFindPlayerFlow(Request),
        [](const FAppError& Error)
        {
            return MWorldClientFlows::BuildClientFailureResponse<FClientFindPlayerResponse>(Error, "player_find_failed");
        });
}

void MWorldClientServiceEndpoint::Client_Logout(FClientLogoutRequest& Request, FClientLogoutResponse& Response)
{
    const SClientCallContext Context = CaptureCurrentClientCallContext();
    if (!Context.IsValid())
    {
        Response.Error = "client_call_context_missing";
        return;
    }

    (void)MClientCallAsyncSupport::StartDeferred<FClientLogoutResponse>(
        Context,
        StartClientLogoutFlow(Request),
        [](const FAppError& Error)
        {
            return MWorldClientFlows::BuildClientFailureResponse<FClientLogoutResponse>(Error, "player_logout_failed");
        });
}

void MWorldClientServiceEndpoint::Client_SwitchScene(
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
        StartClientSwitchSceneFlow(Request),
        [](const FAppError& Error)
        {
            return MWorldClientFlows::BuildClientFailureResponse<FClientSwitchSceneResponse>(Error, "player_switch_scene_failed");
        });
}

MFuture<TResult<FClientLoginResponse, FAppError>> MWorldClientServiceEndpoint::StartClientLoginFlow(
    const FClientLoginRequest& Request,
    uint64 GatewayConnectionId)
{
    if (Request.PlayerId == 0)
    {
        return MClientCallAsyncSupport::MakeErrorFuture<FClientLoginResponse>("player_id_required", "Client_Login");
    }

    if (GatewayConnectionId == 0)
    {
        return MClientCallAsyncSupport::MakeErrorFuture<FClientLoginResponse>("gateway_connection_id_required", "Client_Login");
    }

    if (!LoginRpc || !LoginRpc->IsAvailable())
    {
        return MClientCallAsyncSupport::MakeErrorFuture<FClientLoginResponse>("login_server_unavailable", "Client_Login");
    }

    if (!PlayerService)
    {
        return MClientCallAsyncSupport::MakeErrorFuture<FClientLoginResponse>("world_player_service_missing", "Client_Login");
    }

    return MClientCallAsyncSupport::StartWorkflow<MWorldClientFlows::FClientLoginWorkflow>(
        LoginRpc,
        PlayerService,
        Request,
        GatewayConnectionId);
}

MFuture<TResult<FClientFindPlayerResponse, FAppError>> MWorldClientServiceEndpoint::StartClientFindPlayerFlow(
    const FClientFindPlayerRequest& Request)
{
    if (Request.PlayerId == 0)
    {
        return MClientCallAsyncSupport::MakeErrorFuture<FClientFindPlayerResponse>("player_id_required", "Client_FindPlayer");
    }

    if (!PlayerService)
    {
        return MClientCallAsyncSupport::MakeErrorFuture<FClientFindPlayerResponse>("world_player_service_missing", "Client_FindPlayer");
    }

    FPlayerFindRequest FindRequest;
    FindRequest.PlayerId = Request.PlayerId;
    return MClientCallAsyncSupport::Map(
        PlayerService->PlayerFind(FindRequest),
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

MFuture<TResult<FClientLogoutResponse, FAppError>> MWorldClientServiceEndpoint::StartClientLogoutFlow(
    const FClientLogoutRequest& Request)
{
    if (Request.PlayerId == 0)
    {
        return MClientCallAsyncSupport::MakeErrorFuture<FClientLogoutResponse>("player_id_required", "Client_Logout");
    }

    if (!PlayerService)
    {
        return MClientCallAsyncSupport::MakeErrorFuture<FClientLogoutResponse>("world_player_service_missing", "Client_Logout");
    }

    FPlayerLogoutRequest LogoutRequest;
    LogoutRequest.PlayerId = Request.PlayerId;
    return MClientCallAsyncSupport::Map(
        PlayerService->PlayerLogout(LogoutRequest),
        [](const FPlayerLogoutResponse& LogoutValue)
        {
            FClientLogoutResponse Response;
            Response.bSuccess = true;
            Response.PlayerId = LogoutValue.PlayerId;
            return Response;
        });
}

MFuture<TResult<FClientSwitchSceneResponse, FAppError>> MWorldClientServiceEndpoint::StartClientSwitchSceneFlow(
    const FClientSwitchSceneRequest& Request)
{
    if (Request.PlayerId == 0)
    {
        return MClientCallAsyncSupport::MakeErrorFuture<FClientSwitchSceneResponse>("player_id_required", "Client_SwitchScene");
    }

    if (!PlayerService)
    {
        return MClientCallAsyncSupport::MakeErrorFuture<FClientSwitchSceneResponse>("world_player_service_missing", "Client_SwitchScene");
    }

    FPlayerSwitchSceneRequest SwitchRequest;
    SwitchRequest.PlayerId = Request.PlayerId;
    SwitchRequest.SceneId = Request.SceneId;
    return MClientCallAsyncSupport::Map(
        PlayerService->PlayerSwitchScene(SwitchRequest),
        [](const FPlayerSwitchSceneResponse& SwitchValue)
        {
            FClientSwitchSceneResponse Response;
            Response.bSuccess = true;
            Response.PlayerId = SwitchValue.PlayerId;
            Response.SceneId = SwitchValue.SceneId;
            return Response;
        });
}
