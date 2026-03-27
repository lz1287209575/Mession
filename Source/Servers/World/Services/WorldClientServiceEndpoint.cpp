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

    static const char* FailureCode()
    {
        return "player_find_failed";
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

    static const char* FailureCode()
    {
        return "player_logout_failed";
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

    static const char* FailureCode()
    {
        return "player_switch_scene_failed";
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

template<>
struct TClientPlayerCallBindingTraits<FClientChangeGoldRequest>
{
    using TClientResponse = FClientChangeGoldResponse;
    using TPlayerRequest = FPlayerChangeGoldRequest;
    using TPlayerResponse = FPlayerChangeGoldResponse;

    static const char* ClientFunctionName()
    {
        return "Client_ChangeGold";
    }

    static const char* FailureCode()
    {
        return "player_change_gold_failed";
    }

    static MFuture<TResult<TPlayerResponse, FAppError>> Invoke(
        MWorldPlayerServiceEndpoint* PlayerService,
        const TPlayerRequest& Request)
    {
        return PlayerService->PlayerChangeGold(Request);
    }

    static TPlayerRequest BuildPlayerRequest(const FClientChangeGoldRequest& Request)
    {
        TPlayerRequest OutRequest;
        OutRequest.PlayerId = Request.PlayerId;
        OutRequest.DeltaGold = Request.DeltaGold;
        return OutRequest;
    }

    static TClientResponse BuildClientResponse(const TPlayerResponse& ResponseValue)
    {
        TClientResponse Response;
        Response.bSuccess = true;
        Response.PlayerId = ResponseValue.PlayerId;
        Response.Gold = ResponseValue.Gold;
        return Response;
    }
};

template<>
struct TClientPlayerCallBindingTraits<FClientEquipItemRequest>
{
    using TClientResponse = FClientEquipItemResponse;
    using TPlayerRequest = FPlayerEquipItemRequest;
    using TPlayerResponse = FPlayerEquipItemResponse;

    static const char* ClientFunctionName()
    {
        return "Client_EquipItem";
    }

    static const char* FailureCode()
    {
        return "player_equip_item_failed";
    }

    static MFuture<TResult<TPlayerResponse, FAppError>> Invoke(
        MWorldPlayerServiceEndpoint* PlayerService,
        const TPlayerRequest& Request)
    {
        return PlayerService->PlayerEquipItem(Request);
    }

    static TPlayerRequest BuildPlayerRequest(const FClientEquipItemRequest& Request)
    {
        TPlayerRequest OutRequest;
        OutRequest.PlayerId = Request.PlayerId;
        OutRequest.EquippedItem = Request.EquippedItem;
        return OutRequest;
    }

    static TClientResponse BuildClientResponse(const TPlayerResponse& ResponseValue)
    {
        TClientResponse Response;
        Response.bSuccess = true;
        Response.PlayerId = ResponseValue.PlayerId;
        Response.EquippedItem = ResponseValue.EquippedItem;
        return Response;
    }
};

template<>
struct TClientPlayerCallBindingTraits<FClientGrantExperienceRequest>
{
    using TClientResponse = FClientGrantExperienceResponse;
    using TPlayerRequest = FPlayerGrantExperienceRequest;
    using TPlayerResponse = FPlayerGrantExperienceResponse;

    static const char* ClientFunctionName()
    {
        return "Client_GrantExperience";
    }

    static const char* FailureCode()
    {
        return "player_grant_experience_failed";
    }

    static MFuture<TResult<TPlayerResponse, FAppError>> Invoke(
        MWorldPlayerServiceEndpoint* PlayerService,
        const TPlayerRequest& Request)
    {
        return PlayerService->PlayerGrantExperience(Request);
    }

    static TPlayerRequest BuildPlayerRequest(const FClientGrantExperienceRequest& Request)
    {
        TPlayerRequest OutRequest;
        OutRequest.PlayerId = Request.PlayerId;
        OutRequest.ExperienceDelta = Request.ExperienceDelta;
        return OutRequest;
    }

    static TClientResponse BuildClientResponse(const TPlayerResponse& ResponseValue)
    {
        TClientResponse Response;
        Response.bSuccess = true;
        Response.PlayerId = ResponseValue.PlayerId;
        Response.Level = ResponseValue.Level;
        Response.Experience = ResponseValue.Experience;
        return Response;
    }
};

template<>
struct TClientPlayerCallBindingTraits<FClientModifyHealthRequest>
{
    using TClientResponse = FClientModifyHealthResponse;
    using TPlayerRequest = FPlayerModifyHealthRequest;
    using TPlayerResponse = FPlayerModifyHealthResponse;

    static const char* ClientFunctionName()
    {
        return "Client_ModifyHealth";
    }

    static const char* FailureCode()
    {
        return "player_modify_health_failed";
    }

    static MFuture<TResult<TPlayerResponse, FAppError>> Invoke(
        MWorldPlayerServiceEndpoint* PlayerService,
        const TPlayerRequest& Request)
    {
        return PlayerService->PlayerModifyHealth(Request);
    }

    static TPlayerRequest BuildPlayerRequest(const FClientModifyHealthRequest& Request)
    {
        TPlayerRequest OutRequest;
        OutRequest.PlayerId = Request.PlayerId;
        OutRequest.HealthDelta = Request.HealthDelta;
        return OutRequest;
    }

    static TClientResponse BuildClientResponse(const TPlayerResponse& ResponseValue)
    {
        TClientResponse Response;
        Response.bSuccess = true;
        Response.PlayerId = ResponseValue.PlayerId;
        Response.Health = ResponseValue.Health;
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

template<typename TClientRequest>
void BeginDeferredBoundPlayerClientCall(
    MWorldPlayerServiceEndpoint* PlayerService,
    const TClientRequest& Request,
    typename TClientPlayerCallBindingTraits<TClientRequest>::TClientResponse& Response)
{
    using TBinding = TClientPlayerCallBindingTraits<TClientRequest>;
    using TClientResponse = typename TBinding::TClientResponse;

    const SClientCallContext Context = CaptureCurrentClientCallContext();
    if (!Context.IsValid())
    {
        Response.Error = "client_call_context_missing";
        return;
    }

    (void)MClientCallAsyncSupport::StartDeferred<TClientResponse>(
        Context,
        StartBoundPlayerClientFlow(PlayerService, Request),
        [](const FAppError& Error)
        {
            return BuildClientFailureResponse<TClientResponse>(Error, TBinding::FailureCode());
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
    MWorldClientFlows::BeginDeferredBoundPlayerClientCall(PlayerService, Request, Response);
}

void MWorldClientServiceEndpoint::Client_Logout(FClientLogoutRequest& Request, FClientLogoutResponse& Response)
{
    MWorldClientFlows::BeginDeferredBoundPlayerClientCall(PlayerService, Request, Response);
}

void MWorldClientServiceEndpoint::Client_SwitchScene(
    FClientSwitchSceneRequest& Request,
    FClientSwitchSceneResponse& Response)
{
    MWorldClientFlows::BeginDeferredBoundPlayerClientCall(PlayerService, Request, Response);
}

void MWorldClientServiceEndpoint::Client_ChangeGold(
    FClientChangeGoldRequest& Request,
    FClientChangeGoldResponse& Response)
{
    MWorldClientFlows::BeginDeferredBoundPlayerClientCall(PlayerService, Request, Response);
}

void MWorldClientServiceEndpoint::Client_EquipItem(
    FClientEquipItemRequest& Request,
    FClientEquipItemResponse& Response)
{
    MWorldClientFlows::BeginDeferredBoundPlayerClientCall(PlayerService, Request, Response);
}

void MWorldClientServiceEndpoint::Client_GrantExperience(
    FClientGrantExperienceRequest& Request,
    FClientGrantExperienceResponse& Response)
{
    MWorldClientFlows::BeginDeferredBoundPlayerClientCall(PlayerService, Request, Response);
}

void MWorldClientServiceEndpoint::Client_ModifyHealth(
    FClientModifyHealthRequest& Request,
    FClientModifyHealthResponse& Response)
{
    MWorldClientFlows::BeginDeferredBoundPlayerClientCall(PlayerService, Request, Response);
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
