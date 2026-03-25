#include "Servers/Login/Services/LoginSessionServiceEndpoint.h"

void MLoginSessionServiceEndpoint::Initialize(TMap<uint64, uint32>* InSessions, uint32* InNextSessionKey)
{
    Sessions = InSessions;
    NextSessionKey = InNextSessionKey;
}

MFuture<TResult<FLoginIssueSessionResponse, FAppError>> MLoginSessionServiceEndpoint::IssueSession(
    const FLoginIssueSessionRequest& Request)
{
    if (!Sessions || !NextSessionKey)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FLoginIssueSessionResponse>("login_service_not_initialized", "IssueSession");
    }

    return Implementation.IssueSession(*Sessions, *NextSessionKey, Request);
}

MFuture<TResult<FLoginValidateSessionResponse, FAppError>> MLoginSessionServiceEndpoint::ValidateSessionCall(
    const FLoginValidateSessionRequest& Request)
{
    if (!Sessions)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FLoginValidateSessionResponse>(
            "login_service_not_initialized",
            "ValidateSessionCall");
    }

    return Implementation.ValidateSession(*Sessions, Request);
}
