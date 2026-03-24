#pragma once

#include "Common/Runtime/Reflect/Reflection.h"
#include "Protocol/Messages/Auth/AuthSessionMessages.h"
#include "Servers/App/LoginSessionService.h"

MCLASS(Type=Service)
class MLoginSessionServiceEndpoint : public MObject
{
public:
    MGENERATED_BODY(MLoginSessionServiceEndpoint, MObject, 0)
public:
    void Initialize(TMap<uint64, uint32>* InSessions, uint32* InNextSessionKey);

    MFUNCTION(ServerCall)
    MFuture<TResult<FLoginIssueSessionResponse, FAppError>> IssueSession(const FLoginIssueSessionRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FLoginValidateSessionResponse, FAppError>> ValidateSessionCall(const FLoginValidateSessionRequest& Request);

private:
    TMap<uint64, uint32>* Sessions = nullptr;
    uint32* NextSessionKey = nullptr;
    MLoginSessionService Implementation;
};
