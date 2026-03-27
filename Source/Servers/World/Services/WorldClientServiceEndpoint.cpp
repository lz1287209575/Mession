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

template<typename TClientRequest>
struct TClientPlayerCallBindingTraits;

template<>
struct TClientPlayerCallBindingTraits<FClientFindPlayerRequest>
{
    using TClientResponse = FClientFindPlayerResponse;
    using TPlayerRequest = FPlayerFindRequest;
    using TPlayerResponse = FPlayerFindResponse;

    static const char* ClientFunctionName()
    {
        return "Client_FindPlayer";
    }

    static MFuture<TResult<TPlayerResponse, FAppError>> Invoke(
        MWorldPlayerServiceEndpoint* PlayerService,
        const TPlayerRequest& Request)
    {
        return PlayerService->PlayerFind(Request);
    }

    static TPlayerRequest BuildPlayerRequest(const FClientFindPlayerRequest& Request)
    {
        TPlayerRequest OutRequest;
        OutRequest.PlayerId = Request.PlayerId;
        return OutRequest;
    }

    static TClientResponse BuildClientResponse(const TPlayerResponse& ResponseValue)
    {
        TClientResponse Response;
        Response.bFound = ResponseValue.bFound;
        Response.PlayerId = ResponseValue.PlayerId;
        Response.GatewayConnectionId = ResponseValue.GatewayConnectionId;
        Response.SceneId = ResponseValue.SceneId;
        return Response;
    }
};

template<>
struct TClientPlayerCallBindingTraits<FClientLogoutRequest>
{
    using TClientResponse = FClientLogoutResponse;
    using TPlayerRequest = FPlayerLogoutRequest;
    using TPlayerResponse = FPlayerLogoutResponse;

    static const char* ClientFunctionName()
    {
        return "Client_Logout";
    }

    static MFuture<TResult<TPlayerResponse, FAppError>> Invoke(
        MWorldPlayerServiceEndpoint* PlayerService,
        const TPlayerRequest& Request)
    {
        return PlayerService->PlayerLogout(Request);
    }

    static TPlayerRequest BuildPlayerRequest(const FClientLogoutRequest& Request)
    {
        TPlayerRequest OutRequest;
        OutRequest.PlayerId = Request.PlayerId;
        return OutRequest;
    }

    static TClientResponse BuildClientResponse(const TPlayerResponse& ResponseValue)
    {
        TClientResponse Response;
        Response.bSuccess = true;
        Response.PlayerId = ResponseValue.PlayerId;
        return Response;
    }
};

template<>
struct TClientPlayerCallBindingTraits<FClientSwitchSceneRequest>
{
    using TClientResponse = FClientSwitchSceneResponse;
    using TPlayerRequest = FPlayerSwitchSceneRequest;
    using TPlayerResponse = FPlayerSwitchSceneResponse;

    static const char* ClientFunctionName()
    {
        return "Client_SwitchScene";
    }

    static MFuture<TResult<TPlayerResponse, FAppError>> Invoke(
        MWorldPlayerServiceEndpoint* PlayerService,
        const TPlayerRequest& Request)
    {
        return PlayerService->PlayerSwitchScene(Request);
    }

    static TPlayerRequest BuildPlayerRequest(const FClientSwitchSceneRequest& Request)
    {
        TPlayerRequest OutRequest;
        OutRequest.PlayerId = Request.PlayerId;
        OutRequest.SceneId = Request.SceneId;
        return OutRequest;
    }

    static TClientResponse BuildClientResponse(const TPlayerResponse& ResponseValue)
    {
        TClientResponse Response;
        Response.bSuccess = true;
        Response.PlayerId = ResponseValue.PlayerId;
        Response.SceneId = ResponseValue.SceneId;
        return Response;
    }
};

template<typename TClientRequest>
MFuture<TResult<typename TClientPlayerCallBindingTraits<TClientRequest>::TClientResponse, FAppError>> StartBoundPlayerClientFlow(
    MWorldPlayerServiceEndpoint* PlayerService,
    const TClientRequest& Request)
{
    using TBinding = TClientPlayerCallBindingTraits<TClientRequest>;
    using TClientResponse = typename TBinding::TClientResponse;

    if (Request.PlayerId == 0)
    {
        return MClientCallAsyncSupport::MakeErrorFuture<TClientResponse>(
            "player_id_required",
            TBinding::ClientFunctionName());
    }

    if (!PlayerService)
    {
        return MClientCallAsyncSupport::MakeErrorFuture<TClientResponse>(
            "world_player_service_missing",
            TBinding::ClientFunctionName());
    }

    const typename TBinding::TPlayerRequest PlayerRequest = TBinding::BuildPlayerRequest(Request);
    return MClientCallAsyncSupport::Map(
        TBinding::Invoke(PlayerService, PlayerRequest),
        [](const typename TBinding::TPlayerResponse& ResponseValue)
        {
            return TBinding::BuildClientResponse(ResponseValue);
        });
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
    return MWorldClientFlows::StartBoundPlayerClientFlow(PlayerService, Request);
}

MFuture<TResult<FClientLogoutResponse, FAppError>> MWorldClientServiceEndpoint::StartClientLogoutFlow(
    const FClientLogoutRequest& Request)
{
    return MWorldClientFlows::StartBoundPlayerClientFlow(PlayerService, Request);
}

MFuture<TResult<FClientSwitchSceneResponse, FAppError>> MWorldClientServiceEndpoint::StartClientSwitchSceneFlow(
    const FClientSwitchSceneRequest& Request)
{
    return MWorldClientFlows::StartBoundPlayerClientFlow(PlayerService, Request);
}
