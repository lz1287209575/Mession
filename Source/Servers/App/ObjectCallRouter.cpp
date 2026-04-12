#include "Servers/App/ObjectCallRouter.h"

#include "Servers/App/ObjectCall.h"

void MObjectCallRouter::Initialize(MObjectCallRegistry* InRegistry)
{
    Registry = InRegistry;
}

MFuture<TResult<FObjectCallResponse, FAppError>> MObjectCallRouter::DispatchObjectCall(
    const FObjectCallRequest& Request)
{
    if (!Registry)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FObjectCallResponse>(
            "object_proxy_registry_missing",
            "DispatchObjectCall");
    }

    if (Request.FunctionName.empty())
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FObjectCallResponse>(
            "object_proxy_function_name_required",
            "DispatchObjectCall");
    }

    TResult<MObject*, FAppError> ResolveResult = Registry->ResolveTargetObject(Request.Target);
    if (!ResolveResult.IsOk())
    {
        return MServerCallAsyncSupport::MakeResultFuture(
            MakeErrorResult<FObjectCallResponse>(ResolveResult.GetError()));
    }

    return MObjectCall::CallLocalRaw(
        ResolveResult.GetValue(),
        Request.FunctionName.c_str(),
        Request.Payload);
}

