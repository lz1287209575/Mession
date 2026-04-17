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

inline EServerType ResolveTargetServerType(
    const FObjectCallTarget& Target,
    const MObjectCallRegistry* Registry)
{
    if (Target.TargetServerType != EServerType::Unknown)
    {
        return Target.TargetServerType;
    }

    return Registry
        ? Registry->ResolveOwnerServerType(Target.RootType)
        : EServerType::Unknown;
}

inline TResult<MObject*, FAppError> ResolveLocalTargetObject(
    const FObjectCallTarget& Target,
    MObjectCallRegistry* Registry)
{
    if (!Registry)
    {
        return MakeErrorResult<MObject*>(FAppError::Make(
            "object_proxy_registry_missing",
            ""));
    }

    return Registry->ResolveTargetObject(Target);
}

template<typename TResponse>
inline TResult<TResponse, FAppError> ParsePayloadResult(
    const TByteArray& Payload,
    const char* ParseContext,
    const char* ParseErrorCode)
{
    TResponse ResponseValue {};
    auto ParseResult = ParsePayload(Payload, ResponseValue, ParseContext ? ParseContext : "");
    if (!ParseResult.IsOk())
    {
        return MakeErrorResult<TResponse>(FAppError::Make(
            ParseErrorCode ? ParseErrorCode : "object_proxy_response_parse_failed",
            ParseResult.GetError().c_str()));
    }

    return TResult<TResponse, FAppError>::Ok(std::move(ResponseValue));
}

inline TSharedPtr<IServerCallResponseTarget> MakeLocalObjectCallResponseTarget(
    MPromise<TResult<FObjectCallResponse, FAppError>> Promise,
    const TSharedPtr<std::atomic_bool>& bCompleted,
    MString FunctionNameValue)
{
    return MakeShared<MServerCallResponseTarget>(
        []() -> bool
        {
            return true;
        },
        [Promise, bCompleted, FunctionNameValue = std::move(FunctionNameValue)](
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

            const TResult<FAppError, FAppError> ParsedError = ParsePayloadResult<FAppError>(
                ResponsePayload,
                FunctionNameValue.c_str(),
                "object_proxy_error_parse_failed");
            if (!ParsedError.IsOk())
            {
                Promise.SetValue(MakeErrorResult<FObjectCallResponse>(ParsedError.GetError()));
                return true;
            }

            Promise.SetValue(MakeErrorResult<FObjectCallResponse>(ParsedError.GetValue()));
            return true;
        });
}

inline void CompleteLocalObjectCallDispatchFailure(
    const TSharedPtr<std::atomic_bool>& bCompleted,
    MPromise<TResult<FObjectCallResponse, FAppError>> Promise,
    const char* FunctionName)
{
    bool bExpected = false;
    if (!bCompleted || !bCompleted->compare_exchange_strong(bExpected, true))
    {
        return;
    }

    Promise.SetValue(MakeErrorResult<FObjectCallResponse>(FAppError::Make(
        "object_proxy_dispatch_failed",
        FunctionName ? FunctionName : "")));
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
    const TSharedPtr<IServerCallResponseTarget> ResponseTarget = MakeLocalObjectCallResponseTarget(
        Promise,
        bCompleted,
        MString(FunctionName));

    if (!DispatchServerCall(TargetObject, Function->FunctionId, AllocateLocalCallId(), Payload, ResponseTarget))
    {
        CompleteLocalObjectCallDispatchFailure(bCompleted, Promise, FunctionName);
    }

    return Future;
}

template<typename TResponse>
inline MFuture<TResult<TResponse, FAppError>> ParseRawResponse(
    MFuture<TResult<FObjectCallResponse, FAppError>> RawFuture,
    const char* FunctionName)
{
    return MServerCallAsyncSupport::Map(
        std::move(RawFuture),
        [FunctionNameValue = MString(FunctionName ? FunctionName : "")](
            const FObjectCallResponse& RawResponse) -> TResult<TResponse, FAppError>
        {
            return ParsePayloadResult<TResponse>(
                RawResponse.Payload,
                FunctionNameValue.c_str(),
                "object_proxy_response_parse_failed");
        });
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
    EServerType TargetServerType = MDetail::ResolveTargetServerType(Target, Registry);

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

        TResult<MObject*, FAppError> ResolveResult = MDetail::ResolveLocalTargetObject(Target, Registry);
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
