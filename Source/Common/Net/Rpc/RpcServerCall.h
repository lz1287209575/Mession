#pragma once

#include "Common/Net/Rpc/RpcErrors.h"
#include "Common/Net/Rpc/RpcManifest.h"

#include <utility>

class IServerCallResponseTarget
{
public:
    virtual ~IServerCallResponseTarget() = default;
    virtual bool CanSendServerCallResponse() const = 0;
    virtual bool SendServerCallResponse(uint16 FunctionId, uint64 CallId, bool bSuccess, const TByteArray& Payload) = 0;
};

struct SServerCallContext
{
    uint16 FunctionId = 0;
    uint64 CallId = 0;
    TSharedPtr<IServerCallResponseTarget> ResponseTarget;

    bool IsValid() const
    {
        return FunctionId != 0 && CallId != 0 && ResponseTarget != nullptr;
    }
};

struct SServerCallResponse
{
    bool bSuccess = false;
    TByteArray Payload;
};

class MServerCallResponseTarget final : public IServerCallResponseTarget
{
public:
    MServerCallResponseTarget(
        TFunction<bool()> InCanSend,
        TFunction<bool(uint16, uint64, bool, const TByteArray&)> InSend)
        : CanSendCallback(std::move(InCanSend))
        , SendCallback(std::move(InSend))
    {
    }

    bool CanSendServerCallResponse() const override
    {
        return CanSendCallback ? CanSendCallback() : false;
    }

    bool SendServerCallResponse(uint16 FunctionId, uint64 CallId, bool bSuccess, const TByteArray& Payload) override
    {
        if (!CanSendServerCallResponse())
        {
            return false;
        }
        return SendCallback ? SendCallback(FunctionId, CallId, bSuccess, Payload) : false;
    }

private:
    TFunction<bool()> CanSendCallback;
    TFunction<bool(uint16, uint64, bool, const TByteArray&)> SendCallback;
};

bool TryInvokeServerRpc(MObject* ServiceInstance, const TByteArray& Data, ERpcType ExpectedType);
bool TryInvokeServerRpc(MObject* ServiceInstance, uint64 ConnectionId, const TByteArray& Data, ERpcType ExpectedType);
uint16 PeekServerRpcFunctionId(const TByteArray& Data);
uint64 GetCurrentServerRpcConnectionId();
const MFunction* FindServerCallFunctionByName(const MClass* TargetClass, const char* FunctionName);
const MFunction* FindServerCallFunctionById(const MClass* TargetClass, uint16 FunctionId);
SServerCallContext CaptureCurrentServerCallContext();
bool SendDeferredServerCallResponse(const SServerCallContext& Context, bool bSuccess, const TByteArray& Payload);
bool SendDeferredServerCallSuccessResponse(const SServerCallContext& Context, const TByteArray& Payload);
bool SendDeferredServerCallErrorResponse(const SServerCallContext& Context, const FAppError& Error);
bool DispatchServerCall(
    MObject* TargetInstance,
    uint16 FunctionId,
    uint64 CallId,
    const TByteArray& Payload,
    const TSharedPtr<IServerCallResponseTarget>& ResponseTarget);
bool HandleServerCallResponse(const TByteArray& Data);
uint64 RegisterServerCall(
    TFunction<void(const SServerCallResponse&)> Completion,
    double TimeoutSeconds = 5.0,
    TFunction<bool()> LivenessProbe = {});
void CancelServerCall(uint64 CallId);
bool ConsumeServerCall(uint64 CallId, const SServerCallResponse* Response);
void PumpServerCallMaintenance();
bool BuildServerCallPayload(const MFunction* Function, const TByteArray& RequestPayload, TByteArray& OutData);

inline TFunction<bool()> BuildServerCallLivenessProbe(const TSharedPtr<MServerConnection>& Connection)
{
    TWeakPtr<MServerConnection> WeakConnection = Connection;
    return [WeakConnection]() -> bool
    {
        const TSharedPtr<MServerConnection> Locked = WeakConnection.lock();
        return Locked && Locked->IsConnected();
    };
}

inline TFunction<bool()> BuildServerCallLivenessProbe(const TSharedPtr<INetConnection>& Connection)
{
    TWeakPtr<INetConnection> WeakConnection = Connection;
    return [WeakConnection]() -> bool
    {
        const TSharedPtr<INetConnection> Locked = WeakConnection.lock();
        return Locked && Locked->IsConnected();
    };
}

template<typename TConnection>
inline TFunction<bool()> BuildServerCallLivenessProbe(const TConnection&)
{
    return {};
}

template<typename TResponse, typename TRequest, typename TConnection>
inline MFuture<TResult<TResponse, FAppError>> CallServerFunction(
    TConnection&& Connection,
    const MClass* TargetClass,
    const char* FunctionName,
    const TRequest& Request);

template<typename TResponse, typename TRequest, typename TConnection>
inline MFuture<TResult<TResponse, FAppError>> CallServerFunction(
    TConnection&& Connection,
    EServerType TargetServerType,
    const char* FunctionName,
    const TRequest& Request);

template<typename TResponse, typename TRequest, typename TConnection>
inline MFuture<TResult<TResponse, FAppError>> CallServerFunction(
    TConnection&& Connection,
    const char* TargetClassName,
    const char* FunctionName,
    const TRequest& Request);

template<typename TRequest>
inline bool BuildServerCallPayload(const MFunction* Function, const TRequest& Request, TByteArray& OutData)
{
    if (!Function)
    {
        return false;
    }

    return BuildServerCallPayload(Function, BuildPayload(Request), OutData);
}

template<typename TRequest>
inline bool BuildServerCallPayloadByName(const MClass* TargetClass, const char* FunctionName, const TRequest& Request, TByteArray& OutData)
{
    const MFunction* Function = FindServerCallFunctionByName(TargetClass, FunctionName);
    if (!Function)
    {
        return false;
    }

    return BuildServerCallPayload(Function, Request, OutData);
}

template<typename TResponse, typename TRequest, typename TConnection>
inline MFuture<TResult<TResponse, FAppError>> CallServerFunction(
    TConnection&& Connection,
    const MClass* TargetClass,
    const char* FunctionName,
    const TRequest& Request)
{
    MPromise<TResult<TResponse, FAppError>> Promise;
    MFuture<TResult<TResponse, FAppError>> Future = Promise.GetFuture();

    const MFunction* Function = FindServerCallFunctionByName(TargetClass, FunctionName);
    if (!Function)
    {
        Promise.SetValue(MakeRpcErrorResult<TResponse>("server_call_missing_function", FunctionName ? FunctionName : ""));
        return Future;
    }

    TByteArray RequestPacket;
    if (!BuildServerCallPayload(Function, Request, RequestPacket))
    {
        Promise.SetValue(MakeRpcErrorResult<TResponse>("server_call_build_failed", Function->Name.c_str()));
        return Future;
    }

    MPromise<TResult<TResponse, FAppError>> CompletionPromise = Promise;
    const uint64 CallId = RegisterServerCall(
        [Promise = std::move(CompletionPromise), FunctionNameValue = MString(Function->Name)](const SServerCallResponse& Response) mutable
        {
            if (Response.bSuccess)
            {
                TResponse ResponseValue {};
                auto ParseResult = ParsePayload(Response.Payload, ResponseValue, FunctionNameValue.c_str());
                if (!ParseResult.IsOk())
                {
                    Promise.SetValue(MakeRpcErrorResult<TResponse>("server_call_response_parse_failed", ParseResult.GetError().c_str()));
                    return;
                }

                Promise.SetValue(TResult<TResponse, FAppError>::Ok(std::move(ResponseValue)));
                return;
            }

            FAppError Error;
            auto ParseResult = ParsePayload(Response.Payload, Error, FunctionNameValue.c_str());
            if (!ParseResult.IsOk())
            {
                Promise.SetValue(MakeRpcErrorResult<TResponse>("server_call_error_parse_failed", ParseResult.GetError().c_str()));
                return;
            }

            Promise.SetValue(MakeErrorResult<TResponse>(std::move(Error)));
        },
        5.0,
        BuildServerCallLivenessProbe(Connection));

    if (CallId == 0)
    {
        Promise.SetValue(MakeRpcErrorResult<TResponse>("server_call_register_failed", Function->Name.c_str()));
        return Future;
    }

    TByteArray PacketPayload;
    if (!BuildServerCallPacket(Function->FunctionId, CallId, RequestPacket, PacketPayload))
    {
        ConsumeServerCall(CallId, nullptr);
        Promise.SetValue(MakeRpcErrorResult<TResponse>("server_call_packet_build_failed", Function->Name.c_str()));
        return Future;
    }

    if (!SendServerCallMessage(std::forward<TConnection>(Connection), PacketPayload))
    {
        ConsumeServerCall(CallId, nullptr);
        Promise.SetValue(MakeRpcErrorResult<TResponse>("server_call_send_failed", Function->Name.c_str()));
    }

    return Future;
}

template<typename TResponse, typename TRequest, typename TConnection>
inline MFuture<TResult<TResponse, FAppError>> CallServerFunction(
    TConnection&& Connection,
    EServerType TargetServerType,
    const char* FunctionName,
    const TRequest& Request)
{
    SRpcEndpointBinding Binding;
    const char* TargetClassName = nullptr;
    if (FindRpcEndpoint(TargetServerType, FunctionName, Binding))
    {
        TargetClassName = Binding.ClassName;
    }
    else
    {
        TargetClassName = GetServerEndpointClassName(TargetServerType);
        if (!TargetClassName)
        {
            ReportUnsupportedRpcEndpoint(TargetServerType, FunctionName);
            return MakeRpcErrorFuture<TResponse>("server_call_missing_function", FunctionName ? FunctionName : "");
        }
    }

    return CallServerFunction<TResponse>(
        std::forward<TConnection>(Connection),
        TargetClassName,
        FunctionName,
        Request);
}

template<typename TResponse, typename TRequest, typename TConnection>
inline MFuture<TResult<TResponse, FAppError>> CallServerFunction(
    TConnection&& Connection,
    const char* TargetClassName,
    const char* FunctionName,
    const TRequest& Request)
{
    if (!TargetClassName || !TargetClassName[0] || !FunctionName || !FunctionName[0])
    {
        return MakeRpcErrorFuture<TResponse>("server_call_missing_function", FunctionName ? FunctionName : "");
    }

    const uint16 FunctionId = MGET_STABLE_RPC_FUNCTION_ID(TargetClassName, FunctionName);
    if (FunctionId == 0)
    {
        return MakeRpcErrorFuture<TResponse>("server_call_missing_function", FunctionName);
    }

    MPromise<TResult<TResponse, FAppError>> Promise;
    MFuture<TResult<TResponse, FAppError>> Future = Promise.GetFuture();
    const TByteArray RequestPayload = BuildPayload(Request);

    MPromise<TResult<TResponse, FAppError>> CompletionPromise = Promise;
    const uint64 CallId = RegisterServerCall(
        [Promise = std::move(CompletionPromise), FunctionNameValue = MString(FunctionName)](const SServerCallResponse& Response) mutable
        {
            if (Response.bSuccess)
            {
                TResponse ResponseValue {};
                auto ParseResult = ParsePayload(Response.Payload, ResponseValue, FunctionNameValue.c_str());
                if (!ParseResult.IsOk())
                {
                    Promise.SetValue(MakeRpcErrorResult<TResponse>("server_call_response_parse_failed", ParseResult.GetError().c_str()));
                    return;
                }

                Promise.SetValue(TResult<TResponse, FAppError>::Ok(std::move(ResponseValue)));
                return;
            }

            FAppError ErrorValue;
            auto ParseResult = ParsePayload(Response.Payload, ErrorValue, FunctionNameValue.c_str());
            if (!ParseResult.IsOk())
            {
                Promise.SetValue(MakeRpcErrorResult<TResponse>("server_call_error_parse_failed", ParseResult.GetError().c_str()));
                return;
            }

            Promise.SetValue(MakeErrorResult<TResponse>(std::move(ErrorValue)));
        },
        5.0,
        BuildServerCallLivenessProbe(Connection));

    if (CallId == 0)
    {
        Promise.SetValue(MakeRpcErrorResult<TResponse>("server_call_register_failed", FunctionName));
        return Future;
    }

    TByteArray PacketPayload;
    if (!BuildServerCallPacket(FunctionId, CallId, RequestPayload, PacketPayload))
    {
        CancelServerCall(CallId);
        Promise.SetValue(MakeRpcErrorResult<TResponse>("server_call_build_failed", FunctionName));
        return Future;
    }

    if (!SendServerCallMessage(std::forward<TConnection>(Connection), PacketPayload))
    {
        CancelServerCall(CallId);
        Promise.SetValue(MakeRpcErrorResult<TResponse>("server_call_send_failed", FunctionName));
        return Future;
    }

    return Future;
}
