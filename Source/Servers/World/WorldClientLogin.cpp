#include "Servers/World/WorldClient.h"
#include "Common/Runtime/Concurrency/FiberAwait.h"
#include "Servers/World/WorldClientCommon.h"
#include "Servers/World/Backend/WorldLogin.h"
#include "Servers/World/WorldServer.h"

void MWorldClient::Client_Login(FClientLoginRequest& Request, FClientLoginResponse& Response)
{
    const uint64 GatewayConnectionId = GetCurrentClientConnectionId();
    if (GatewayConnectionId == 0)
    {
        Response.Error = "client_context_missing";
        return;
    }
    (void)MWorldClientCommon::StartAsyncClientResponse(
        Response,
        WorldServer ? WorldServer->GetTaskRunner() : nullptr,
        "client_login_failed",
        [this, Request = FClientLoginRequest(Request), GatewayConnectionId]() mutable
        {
            return DoClientLogin(std::move(Request), GatewayConnectionId);
        });
}

TResult<FClientLoginResponse, FAppError> MWorldClient::DoClientLogin(
    FClientLoginRequest Request,
    uint64 GatewayConnectionId)
{
    if (Request.PlayerId == 0)
    {
        return MakeErrorResult<FClientLoginResponse>(FAppError::Make("player_id_required", "Client_Login"));
    }

    if (GatewayConnectionId == 0)
    {
        return MakeErrorResult<FClientLoginResponse>(FAppError::Make("gateway_connection_id_required", "Client_Login"));
    }

    if (!Login || !Login->IsAvailable())
    {
        return MakeErrorResult<FClientLoginResponse>(FAppError::Make("login_server_unavailable", "Client_Login"));
    }

    if (!WorldServer)
    {
        return MakeErrorResult<FClientLoginResponse>(FAppError::Make("world_server_missing", "Client_Login"));
    }

    MPlayerService* PlayerService = WorldServer->GetPlayerService();
    if (!PlayerService)
    {
        return MakeErrorResult<FClientLoginResponse>(FAppError::Make("player_service_missing", "Client_Login"));
    }

    FLoginIssueSessionRequest IssueRequest;
    IssueRequest.PlayerId = Request.PlayerId;
    IssueRequest.GatewayConnectionId = GatewayConnectionId;

    const FLoginIssueSessionResponse LoginResponse = MAwaitOk(Login->IssueSession(IssueRequest));

    FPlayerEnterWorldRequest EnterRequest;
    EnterRequest.PlayerId = Request.PlayerId;
    EnterRequest.GatewayConnectionId = GatewayConnectionId;
    EnterRequest.SessionKey = LoginResponse.SessionKey;
    (void)MAwaitOk(PlayerService->PlayerEnterWorld(EnterRequest));

    FClientLoginResponse Response;
    Response.PlayerId = Request.PlayerId;
    Response.SessionKey = LoginResponse.SessionKey;
    return MWorldClientCommon::BuildSuccessResult(std::move(Response));
}
