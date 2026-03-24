#pragma once

#include "Common/Runtime/MLib.h"
#include "Protocol/Messages/Auth/AuthSessionMessages.h"
#include "Servers/App/ServerCallAsyncSupport.h"

class MLoginSessionService
{
public:
    MFuture<TResult<FLoginIssueSessionResponse, FAppError>> IssueSession(
        TMap<uint64, uint32>& Sessions,
        uint32& NextSessionKey,
        const FLoginIssueSessionRequest& Request) const
    {
        if (Request.PlayerId == 0)
        {
            return MServerCallAsyncSupport::MakeErrorFuture<FLoginIssueSessionResponse>("player_id_required", "IssueSession");
        }

        const uint32 SessionKey = NextSessionKey++;
        Sessions[Request.PlayerId] = SessionKey;

        FLoginIssueSessionResponse Response;
        Response.PlayerId = Request.PlayerId;
        Response.SessionKey = SessionKey;
        return MServerCallAsyncSupport::MakeSuccessFuture(std::move(Response));
    }

    MFuture<TResult<FLoginValidateSessionResponse, FAppError>> ValidateSession(
        const TMap<uint64, uint32>& Sessions,
        const FLoginValidateSessionRequest& Request) const
    {
        if (Request.PlayerId == 0 || Request.SessionKey == 0)
        {
            return MServerCallAsyncSupport::MakeErrorFuture<FLoginValidateSessionResponse>("invalid_validate_request", "ValidateSessionCall");
        }

        FLoginValidateSessionResponse Response;
        Response.PlayerId = Request.PlayerId;
        auto It = Sessions.find(Request.PlayerId);
        Response.bValid = It != Sessions.end() && It->second == Request.SessionKey;
        return MServerCallAsyncSupport::MakeSuccessFuture(std::move(Response));
    }
};
