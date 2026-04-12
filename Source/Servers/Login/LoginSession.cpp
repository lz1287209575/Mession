#include "Servers/Login/LoginSession.h"

void MLoginSession::Initialize(TMap<uint64, uint32>* InSessions, uint32* InNextSessionKey)
{
    Sessions = InSessions;
    NextSessionKey = InNextSessionKey;
}

MFuture<TResult<FLoginIssueSessionResponse, FAppError>> MLoginSession::IssueSession(
    const FLoginIssueSessionRequest& Request)
{
    if (!Sessions || !NextSessionKey)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FLoginIssueSessionResponse>("login_service_not_initialized", "IssueSession");
    }

    return Implementation.IssueSession(*Sessions, *NextSessionKey, Request);
}

MFuture<TResult<FLoginValidateSessionResponse, FAppError>> MLoginSession::ValidateSession(
    const FLoginValidateSessionRequest& Request)
{
    if (!Sessions)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FLoginValidateSessionResponse>(
            "login_service_not_initialized",
            "ValidateSession");
    }

    return Implementation.ValidateSession(*Sessions, Request);
}

