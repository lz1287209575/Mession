#include "Servers/World/WorldClient.h"
#include "Servers/World/WorldClientCommon.h"
#include "Servers/World/Backend/WorldLogin.h"
#include "Servers/World/WorldServer.h"

namespace MWorldClientLogin
{
class FAction final
    : public MClientCallAsyncSupport::TClientCallAction<FAction, FClientLoginResponse>
{
public:
    using TResponseType = FClientLoginResponse;

    FAction(
        MWorldLogin* InLogin,
        MWorldServer* InWorldServer,
        FClientLoginRequest InRequest,
        uint64 InGatewayConnectionId)
        : Login(InLogin)
        , WorldServer(InWorldServer)
        , Request(std::move(InRequest))
        , GatewayConnectionId(InGatewayConnectionId)
    {
    }

protected:
    void OnStart() override
    {
        if (!Login || !WorldServer)
        {
            Fail("client_login_dependencies_missing", "Client_Login");
            return;
        }

        FLoginIssueSessionRequest IssueRequest;
        IssueRequest.PlayerId = Request.PlayerId;
        IssueRequest.GatewayConnectionId = GatewayConnectionId;
        Continue(Login->IssueSession(IssueRequest), &FAction::OnSessionIssued);
    }

private:
    void OnSessionIssued(const FLoginIssueSessionResponse& LoginResponse)
    {
        SessionKey = LoginResponse.SessionKey;
        MPlayerService* PlayerService = WorldServer ? WorldServer->GetPlayerService() : nullptr;
        if (!PlayerService)
        {
            Fail("player_service_missing", "Client_Login");
            return;
        }

        FPlayerEnterWorldRequest EnterRequest;
        EnterRequest.PlayerId = Request.PlayerId;
        EnterRequest.GatewayConnectionId = GatewayConnectionId;
        EnterRequest.SessionKey = SessionKey;
        Continue(PlayerService->PlayerEnterWorld(EnterRequest), &FAction::OnPlayerEnteredWorld);
    }

    void OnPlayerEnteredWorld(const FPlayerEnterWorldResponse&)
    {
        FClientLoginResponse Response;
        Response.bSuccess = true;
        Response.PlayerId = Request.PlayerId;
        Response.SessionKey = SessionKey;
        Succeed(std::move(Response));
    }

    MWorldLogin* Login = nullptr;
    MWorldServer* WorldServer = nullptr;
    FClientLoginRequest Request;
    uint64 GatewayConnectionId = 0;
    uint32 SessionKey = 0;
};
} // namespace MWorldClientLogin

void MWorldClient::Client_Login(FClientLoginRequest& Request, FClientLoginResponse& Response)
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
        StartClientLogin(Request, GatewayConnectionId),
        [](const FAppError& Error)
        {
            return MWorldClientCommon::BuildFailureResponse<FClientLoginResponse>(Error, "client_login_failed");
        });
}

MFuture<TResult<FClientLoginResponse, FAppError>> MWorldClient::StartClientLogin(
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

    if (!Login || !Login->IsAvailable())
    {
        return MClientCallAsyncSupport::MakeErrorFuture<FClientLoginResponse>("login_server_unavailable", "Client_Login");
    }

    if (!WorldServer)
    {
        return MClientCallAsyncSupport::MakeErrorFuture<FClientLoginResponse>("world_server_missing", "Client_Login");
    }

    if (!WorldServer->GetPlayerService())
    {
        return MClientCallAsyncSupport::MakeErrorFuture<FClientLoginResponse>("player_service_missing", "Client_Login");
    }

    return MClientCallAsyncSupport::StartAction<MWorldClientLogin::FAction>(
        Login,
        WorldServer,
        Request,
        GatewayConnectionId);
}
