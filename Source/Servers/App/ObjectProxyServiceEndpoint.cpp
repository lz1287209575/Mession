#include "Servers/App/ObjectProxyServiceEndpoint.h"

#include "Servers/App/ObjectProxyCall.h"

void MObjectProxyServiceEndpoint::Initialize(MObjectProxyRegistry* InRegistry)
{
    Registry = InRegistry;
}

MFuture<TResult<FObjectProxyInvokeResponse, FAppError>> MObjectProxyServiceEndpoint::InvokeObjectCall(
    const FObjectProxyInvokeRequest& Request)
{
    if (!Registry)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FObjectProxyInvokeResponse>(
            "object_proxy_registry_missing",
            "InvokeObjectCall");
    }

    if (Request.FunctionName.empty())
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FObjectProxyInvokeResponse>(
            "object_proxy_function_name_required",
            "InvokeObjectCall");
    }

    TResult<MObject*, FAppError> ResolveResult = Registry->ResolveTargetObject(Request.Target);
    if (!ResolveResult.IsOk())
    {
        return MServerCallAsyncSupport::MakeResultFuture(
            MakeErrorResult<FObjectProxyInvokeResponse>(ResolveResult.GetError()));
    }

    return MObjectProxyCall::CallLocalRaw(
        ResolveResult.GetValue(),
        Request.FunctionName.c_str(),
        Request.Payload);
}
