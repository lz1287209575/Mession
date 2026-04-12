#pragma once

#include "Common/Net/Rpc/RpcDispatch.h"
#include "Common/Runtime/Concurrency/Promise.h"
#include "Common/Runtime/Object/Result.h"
#include "Protocol/Messages/Common/AppMessages.h"
#include "Protocol/Messages/Common/ForwardedClientCallMessages.h"

namespace MClientDispatch
{
inline FAppError MakeDispatchError(const SClientDispatchOutcome& Outcome)
{
    const char* FunctionName =
        (Outcome.FunctionName && Outcome.FunctionName[0] != '\0') ? Outcome.FunctionName : "ClientCall";

    switch (Outcome.Result)
    {
    case EClientDispatchResult::NotFound:
        return FAppError::Make("client_call_not_found", FunctionName);
    case EClientDispatchResult::MissingFunction:
        return FAppError::Make("client_call_missing_function", FunctionName);
    case EClientDispatchResult::MissingBinder:
        return FAppError::Make("client_call_missing_binder", FunctionName);
    case EClientDispatchResult::ParamBindingFailed:
        return FAppError::Make("client_call_param_binding_failed", FunctionName);
    case EClientDispatchResult::InvokeFailed:
        return FAppError::Make("client_call_invoke_failed", FunctionName);
    case EClientDispatchResult::AuthRequired:
        return FAppError::Make("client_call_auth_required", FunctionName);
    case EClientDispatchResult::RouteTargetUnsupported:
        return FAppError::Make("client_call_route_unsupported", FunctionName);
    case EClientDispatchResult::BackendUnavailable:
        return FAppError::Make("client_call_backend_unavailable", FunctionName);
    default:
        return FAppError::Make("client_call_dispatch_failed", FunctionName);
    }
}

class FClientDispatchResponseTarget final : public IClientResponseTarget
{
public:
    FClientDispatchResponseTarget(
        uint64 InConnectionId,
        MPromise<TResult<FForwardedClientCallResponse, FAppError>> InPromise)
        : ConnectionId(InConnectionId)
        , Promise(std::move(InPromise))
    {
    }

    bool CanSendClientResponse(uint64 InConnectionId) const override
    {
        return !bResolved.load() && InConnectionId == ConnectionId;
    }

    bool SendClientResponse(
        uint64 InConnectionId,
        uint16 /*FunctionId*/,
        uint64 /*CallId*/,
        const TByteArray& Payload) override
    {
        if (!CanSendClientResponse(InConnectionId))
        {
            return false;
        }

        if (bResolved.exchange(true))
        {
            return false;
        }

        FForwardedClientCallResponse Response;
        Response.Payload = Payload;
        Promise.SetValue(TResult<FForwardedClientCallResponse, FAppError>::Ok(std::move(Response)));
        return true;
    }

    bool IsResolved() const
    {
        return bResolved.load();
    }

private:
    uint64 ConnectionId = 0;
    MPromise<TResult<FForwardedClientCallResponse, FAppError>> Promise;
    std::atomic<bool> bResolved { false };
};

inline MFuture<TResult<FForwardedClientCallResponse, FAppError>> DispatchCall(
    MObject* TargetInstance,
    const FForwardedClientCallRequest& Request)
{
    MPromise<TResult<FForwardedClientCallResponse, FAppError>> Promise;
    MFuture<TResult<FForwardedClientCallResponse, FAppError>> Future = Promise.GetFuture();

    if (!TargetInstance)
    {
        Promise.SetValue(MakeErrorResult<FForwardedClientCallResponse>(
            FAppError::Make("client_call_target_missing", "DispatchClientCall")));
        return Future;
    }

    const TSharedPtr<FClientDispatchResponseTarget> ResponseTarget =
        MakeShared<FClientDispatchResponseTarget>(Request.GatewayConnectionId, Promise);

    const SClientDispatchOutcome Outcome = DispatchClientFunction(
        TargetInstance,
        Request.GatewayConnectionId,
        Request.ClientFunctionId,
        Request.ClientCallId,
        Request.Payload,
        ResponseTarget);

    if (Outcome.Result == EClientDispatchResult::Handled)
    {
        return Future;
    }

    if (!ResponseTarget->IsResolved())
    {
        Promise.SetValue(MakeErrorResult<FForwardedClientCallResponse>(MakeDispatchError(Outcome)));
    }

    return Future;
}
}

