#pragma once

#include "Common/Net/Rpc/RpcRuntimeContext.h"
#include "Common/Net/Rpc/RpcServerCall.h"
#include "Common/Net/ServerConnection.h"
#include "Common/Runtime/Object/Result.h"
#include "Protocol/Messages/Common/ObjectCallMessages.h"
#include "Servers/App/ObjectCallRegistry.h"
#include "Servers/App/ServerCallAsyncSupport.h"

#include <atomic>
#include <exception>

namespace MObjectCall
{
inline MFuture<TResult<FObjectCallResponse, FAppError>> CallRaw(
    const FObjectCallTarget& Target,
    const char* FunctionName,
    const TByteArray& RequestPayload,
    MObject* ContextObject);

template<typename TResponse, typename TRequest>
MFuture<TResult<TResponse, FAppError>> Call(
    const FObjectCallTarget& Target,
    const char* FunctionName,
    const TRequest& Request,
    MObject* ContextObject);

class FBoundTarget
{
public:
    FBoundTarget() = default;

    FBoundTarget(FObjectCallTarget InTarget, MObject* InContextObject)
        : Target(std::move(InTarget))
        , ContextObject(InContextObject)
    {
    }

    const FObjectCallTarget& GetTarget() const
    {
        return Target;
    }

    MObject* GetContextObject() const
    {
        return ContextObject;
    }

    FBoundTarget WithPath(MString InObjectPath) const
    {
        FObjectCallTarget NextTarget = Target;
        NextTarget.ObjectPath = std::move(InObjectPath);
        return FBoundTarget(std::move(NextTarget), ContextObject);
    }

    FBoundTarget Child(const char* Segment) const
    {
        if (!Segment || Segment[0] == '\0')
        {
            return *this;
        }

        MString NextPath = Target.ObjectPath;
        if (!NextPath.empty())
        {
            NextPath += ".";
        }
        NextPath += Segment;
        return WithPath(std::move(NextPath));
    }

    template<typename TResponse, typename TRequest>
    MFuture<TResult<TResponse, FAppError>> Call(
        const char* FunctionName,
        const TRequest& Request) const
    {
        return MObjectCall::Call<TResponse>(Target, FunctionName, Request, ContextObject);
    }

    MFuture<TResult<FObjectCallResponse, FAppError>> CallRaw(
        const char* FunctionName,
        const TByteArray& RequestPayload) const
    {
        return MObjectCall::CallRaw(Target, FunctionName, RequestPayload, ContextObject);
    }

private:
    FObjectCallTarget Target;
    MObject* ContextObject = nullptr;
};

namespace MDetail
{
inline uint64 AllocateLocalCallId()
{
    static std::atomic<uint64> GNextLocalObjectCallId { 1 };
    const uint64 CallId = GNextLocalObjectCallId.fetch_add(1);
    return CallId != 0 ? CallId : GNextLocalObjectCallId.fetch_add(1);
}

inline IServerRuntimeContext* FindServerRuntimeContext(MObject* ContextObject)
{
    for (MObject* Current = ContextObject; Current; Current = Current->GetOuter())
    {
        if (auto* RuntimeContext = dynamic_cast<IServerRuntimeContext*>(Current))
        {
            return RuntimeContext;
        }
    }

    return nullptr;
}

inline MObjectCallRegistry* FindObjectCallRegistry(MObject* ContextObject)
{
    for (MObject* Current = ContextObject; Current; Current = Current->GetOuter())
    {
        if (auto* Provider = dynamic_cast<IObjectCallRegistryProvider*>(Current))
        {
            return Provider->GetObjectCallRegistry();
        }
    }

    return nullptr;
}

inline MFuture<TResult<FObjectCallResponse, FAppError>> DispatchLocalRaw(
    MObject* TargetObject,
    const char* FunctionName,
    const TByteArray& RequestPayload)
{
    if (!TargetObject)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FObjectCallResponse>(
            "object_proxy_target_missing",
            FunctionName ? FunctionName : "");
    }

    if (!FunctionName || FunctionName[0] == '\0')
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FObjectCallResponse>(
            "object_proxy_function_name_required");
    }

    MClass* TargetClass = TargetObject->GetClass();
    if (!TargetClass)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FObjectCallResponse>(
            "object_proxy_target_class_missing",
            FunctionName);
    }

    const MFunction* Function = FindServerCallFunctionByName(TargetClass, FunctionName);
    if (!Function || !Function->ServerCallHandler)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FObjectCallResponse>(
            "object_proxy_function_missing",
            FunctionName);
    }

    TByteArray Payload;
    if (!BuildServerCallPayload(Function, RequestPayload, Payload))
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FObjectCallResponse>(
            "object_proxy_payload_build_failed",
            FunctionName);
    }

    MPromise<TResult<FObjectCallResponse, FAppError>> Promise;
    MFuture<TResult<FObjectCallResponse, FAppError>> Future = Promise.GetFuture();
    const TSharedPtr<std::atomic_bool> bCompleted = MakeShared<std::atomic_bool>(false);

    const TSharedPtr<IServerCallResponseTarget> ResponseTarget =
        MakeShared<MServerCallResponseTarget>(
            []() -> bool
            {
                return true;
            },
            [Promise, bCompleted, FunctionNameValue = MString(FunctionName)](
                uint16,
                uint64,
                bool bSuccess,
                const TByteArray& ResponsePayload) mutable -> bool
            {
                bool bExpected = false;
                if (!bCompleted || !bCompleted->compare_exchange_strong(bExpected, true))
                {
                    return true;
                }

                if (bSuccess)
                {
                    FObjectCallResponse ResponseValue;
                    ResponseValue.Payload = ResponsePayload;
                    Promise.SetValue(TResult<FObjectCallResponse, FAppError>::Ok(std::move(ResponseValue)));
                    return true;
                }

                FAppError ErrorValue;
                auto ParseResult = ParsePayload(ResponsePayload, ErrorValue, FunctionNameValue.c_str());
                if (!ParseResult.IsOk())
                {
                    Promise.SetValue(MakeErrorResult<FObjectCallResponse>(FAppError::Make(
                        "object_proxy_error_parse_failed",
                        ParseResult.GetError().c_str())));
                    return true;
                }

                Promise.SetValue(MakeErrorResult<FObjectCallResponse>(std::move(ErrorValue)));
                return true;
            });

    if (!DispatchServerCall(TargetObject, Function->FunctionId, AllocateLocalCallId(), Payload, ResponseTarget))
    {
        bool bExpected = false;
        if (bCompleted && bCompleted->compare_exchange_strong(bExpected, true))
        {
            Promise.SetValue(MakeErrorResult<FObjectCallResponse>(FAppError::Make(
                "object_proxy_dispatch_failed",
                FunctionName)));
        }
    }

    return Future;
}

template<typename TResponse>
inline MFuture<TResult<TResponse, FAppError>> ParseRawResponse(
    MFuture<TResult<FObjectCallResponse, FAppError>> RawFuture,
    const char* FunctionName)
{
    if (!RawFuture.Valid())
    {
        return MServerCallAsyncSupport::MakeErrorFuture<TResponse>(
            "object_proxy_invalid_future",
            FunctionName ? FunctionName : "");
    }

    MPromise<TResult<TResponse, FAppError>> Promise;
    MFuture<TResult<TResponse, FAppError>> Future = Promise.GetFuture();
    RawFuture.Then(
        [Promise, FunctionNameValue = MString(FunctionName ? FunctionName : "")](
            MFuture<TResult<FObjectCallResponse, FAppError>> Completed) mutable
        {
            try
            {
                TResult<FObjectCallResponse, FAppError> Result = Completed.Get();
                if (!Result.IsOk())
                {
                    Promise.SetValue(MakeErrorResult<TResponse>(Result.GetError()));
                    return;
                }

                TResponse ResponseValue {};
                auto ParseResult = ParsePayload(
                    Result.GetValue().Payload,
                    ResponseValue,
                    FunctionNameValue.c_str());
                if (!ParseResult.IsOk())
                {
                    Promise.SetValue(MakeErrorResult<TResponse>(FAppError::Make(
                        "object_proxy_response_parse_failed",
                        ParseResult.GetError().c_str())));
                    return;
                }

                Promise.SetValue(TResult<TResponse, FAppError>::Ok(std::move(ResponseValue)));
            }
            catch (const std::exception& Ex)
            {
                Promise.SetValue(MakeErrorResult<TResponse>(FAppError::Make(
                    "object_proxy_exception",
                    Ex.what())));
            }
            catch (...)
            {
                Promise.SetValue(MakeErrorResult<TResponse>(FAppError::Make(
                    "object_proxy_exception",
                    "unknown")));
            }
        });
    return Future;
}
} // namespace MDetail

inline MFuture<TResult<FObjectCallResponse, FAppError>> CallLocalRaw(
    MObject* TargetObject,
    const char* FunctionName,
    const TByteArray& RequestPayload)
{
    return MDetail::DispatchLocalRaw(TargetObject, FunctionName, RequestPayload);
}

template<typename TResponse, typename TRequest>
MFuture<TResult<TResponse, FAppError>> CallLocal(
    MObject* TargetObject,
    const char* FunctionName,
    const TRequest& Request)
{
    return MDetail::ParseRawResponse<TResponse>(
        CallLocalRaw(TargetObject, FunctionName, BuildPayload(Request)),
        FunctionName);
}

inline MFuture<TResult<FObjectCallResponse, FAppError>> CallRaw(
    const FObjectCallTarget& Target,
    const char* FunctionName,
    const TByteArray& RequestPayload,
    MObject* ContextObject)
{
    if (!ContextObject)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FObjectCallResponse>(
            "object_proxy_context_missing",
            FunctionName ? FunctionName : "");
    }

    if (!FunctionName || FunctionName[0] == '\0')
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FObjectCallResponse>(
            "object_proxy_function_name_required");
    }

    MObjectCallRegistry* Registry = MDetail::FindObjectCallRegistry(ContextObject);
    EServerType TargetServerType = Target.TargetServerType;
    if (TargetServerType == EServerType::Unknown && Registry)
    {
        TargetServerType = Registry->ResolveOwnerServerType(Target.RootType);
    }

    if (TargetServerType == EServerType::Unknown)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FObjectCallResponse>(
            "object_proxy_target_server_missing",
            FunctionName);
    }

    if (TargetServerType == MServerConnection::GetLocalInfo().ServerType)
    {
        if (!Registry)
        {
            return MServerCallAsyncSupport::MakeErrorFuture<FObjectCallResponse>(
                "object_proxy_registry_missing",
                FunctionName);
        }

        TResult<MObject*, FAppError> ResolveResult = Registry->ResolveTargetObject(Target);
        if (!ResolveResult.IsOk())
        {
            return MServerCallAsyncSupport::MakeResultFuture(
                MakeErrorResult<FObjectCallResponse>(ResolveResult.GetError()));
        }

        return CallLocalRaw(ResolveResult.GetValue(), FunctionName, RequestPayload);
    }

    IServerRuntimeContext* RuntimeContext = MDetail::FindServerRuntimeContext(ContextObject);
    if (!RuntimeContext)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FObjectCallResponse>(
            "object_proxy_runtime_context_missing",
            FunctionName);
    }

    const IRpcTransportResolver* TransportResolver = RuntimeContext->GetRpcTransportResolver();
    const TSharedPtr<MServerConnection> Connection = TransportResolver
        ? TransportResolver->ResolveServerTransport(TargetServerType)
        : nullptr;
    if (!Connection || !Connection->IsConnected())
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FObjectCallResponse>(
            "object_proxy_target_unavailable",
            FunctionName);
    }

    FObjectCallRequest ProxyRequest;
    ProxyRequest.Target = Target;
    ProxyRequest.Target.TargetServerType = TargetServerType;
    ProxyRequest.FunctionName = FunctionName;
    ProxyRequest.Payload = RequestPayload;
    return CallServerFunction<FObjectCallResponse>(
        Connection,
        TargetServerType,
        "DispatchObjectCall",
        ProxyRequest);
}

template<typename TResponse, typename TRequest>
MFuture<TResult<TResponse, FAppError>> Call(
    const FObjectCallTarget& Target,
    const char* FunctionName,
    const TRequest& Request,
    MObject* ContextObject)
{
    return MDetail::ParseRawResponse<TResponse>(
        CallRaw(Target, FunctionName, BuildPayload(Request), ContextObject),
        FunctionName);
}

inline FBoundTarget Bind(const FObjectCallTarget& Target, MObject* ContextObject)
{
    return FBoundTarget(Target, ContextObject);
}
} // namespace MObjectCall

