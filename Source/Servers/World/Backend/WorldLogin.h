#pragma once

#include "Common/Runtime/Reflect/Reflection.h"
#include "Protocol/Messages/Auth/AuthSessionMessages.h"
#include "Servers/App/ServerCallProxy.h"

MCLASS(Type=Rpc)
class MWorldLogin : public MServerCallProxyBase
{
public:
    MGENERATED_BODY(MWorldLogin, MServerCallProxyBase, 0)
public:
    MFUNCTION(ServerCall, Target=Login)
    MFuture<TResult<FLoginIssueSessionResponse, FAppError>> IssueSession(const FLoginIssueSessionRequest& Request);

    MFUNCTION(ServerCall, Target=Login)
    MFuture<TResult<FLoginValidateSessionResponse, FAppError>> ValidateSession(const FLoginValidateSessionRequest& Request);

private:
    EServerType GetTargetServerType() const override;
};


