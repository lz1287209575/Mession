#include "Common/Net/Rpc/RpcServerCall.h"

#include "Common/Runtime/Async/MAsync.h"
#include "Common/Runtime/Async/AsyncContext.h"
#include "Common/Runtime/Log/Log.h"
#include "Common/Runtime/Time.h"

#include <atomic>
#include <cstring>
#include <mutex>

namespace
{
thread_local uint64 GCurrentServerRpcConnectionId = 0;
thread_local SServerCallContext GCurrentServerCallContext;

class FScopedServerRpcConnectionContext
{
public:
    explicit FScopedServerRpcConnectionContext(uint64 InConnectionId)
        : PreviousConnectionId(GCurrentServerRpcConnectionId)
    {
        GCurrentServerRpcConnectionId = InConnectionId;
    }

    ~FScopedServerRpcConnectionContext()
    {
        GCurrentServerRpcConnectionId = PreviousConnectionId;
    }

private:
    uint64 PreviousConnectionId = 0;
};

class FScopedServerCallContext
{
public:
    explicit FScopedServerCallContext(const SServerCallContext& InContext)
        : PreviousContext(GCurrentServerCallContext)
    {
        GCurrentServerCallContext = InContext;
    }

    ~FScopedServerCallContext()
    {
        GCurrentServerCallContext = PreviousContext;
    }

private:
    SServerCallContext PreviousContext;
};

struct SPendingServerCall
{
    TFunction<void(const SServerCallResponse&)> Completion;
    double DeadlineSeconds = 0.0;
    TFunction<bool()> LivenessProbe;
};

std::atomic<uint64> GNextServerCallId{1};
std::mutex GServerCallMutex;
TMap<uint64, SPendingServerCall> GPendingServerCalls;

bool IsServerCallFunction(const MFunction* Function)
{
    return Function && Function->Transport == "ServerCall";
}
}

uint16 PeekServerRpcFunctionId(const TByteArray& Data)
{
    if (Data.size() < sizeof(uint16))
    {
        return 0;
    }

    uint16 FunctionId = 0;
    std::memcpy(&FunctionId, Data.data(), sizeof(FunctionId));
    return FunctionId;
}

uint64 GetCurrentServerRpcConnectionId()
{
    return GCurrentServerRpcConnectionId;
}

SServerCallContext CaptureCurrentServerCallContext()
{
    return GCurrentServerCallContext;
}

bool SendDeferredServerCallResponse(const SServerCallContext& Context, bool bSuccess, const TByteArray& Payload)
{
    if (!Context.IsValid())
    {
        return false;
    }

    if (!Context.ResponseTarget->CanSendServerCallResponse())
    {
        return false;
    }

    return Context.ResponseTarget->SendServerCallResponse(
        Context.FunctionId,
        Context.CallId,
        bSuccess,
        Payload);
}

bool SendDeferredServerCallSuccessResponse(const SServerCallContext& Context, const TByteArray& Payload)
{
    return SendDeferredServerCallResponse(Context, true, Payload);
}

bool SendDeferredServerCallErrorResponse(const SServerCallContext& Context, const FAppError& Error)
{
    return SendDeferredServerCallResponse(Context, false, BuildPayload(Error));
}

bool TryInvokeServerRpc(MObject* ServiceInstance, const TByteArray& Data, ERpcType ExpectedType)
{
    if (!ServiceInstance)
    {
        return false;
    }

    if (Data.size() < sizeof(uint16) + sizeof(uint32))
    {
        LOG_WARN("TryInvokeServerRpc: packet too small (size=%llu)",
                 static_cast<unsigned long long>(Data.size()));
        return false;
    }

    size_t Offset = 0;
    uint16 FunctionId = 0;
    uint32 PayloadSize = 0;

    std::memcpy(&FunctionId, Data.data() + Offset, sizeof(FunctionId));
    Offset += sizeof(FunctionId);

    std::memcpy(&PayloadSize, Data.data() + Offset, sizeof(PayloadSize));
    Offset += sizeof(PayloadSize);

    if (Offset + PayloadSize > Data.size())
    {
        LOG_WARN("TryInvokeServerRpc: payload out of range (size=%llu, payload=%u)",
                 static_cast<unsigned long long>(Data.size()),
                 static_cast<unsigned>(PayloadSize));
        return false;
    }

    MClass* ServiceClass = ServiceInstance->GetClass();
    if (!ServiceClass)
    {
        LOG_ERROR("TryInvokeServerRpc: ServiceInstance has no class");
        return false;
    }

    MFunction* FuncMeta = ServiceClass->FindFunctionById(FunctionId);
    if (!FuncMeta)
    {
        LOG_WARN("TryInvokeServerRpc: unknown FunctionId=%u",
                 static_cast<unsigned>(FunctionId));
        return false;
    }

    if (FuncMeta->RpcType != ExpectedType)
    {
        LOG_WARN("TryInvokeServerRpc: RpcType mismatch for FunctionId=%u (expected=%d, actual=%d)",
                 static_cast<unsigned>(FunctionId),
                 static_cast<int>(ExpectedType),
                 static_cast<int>(FuncMeta->RpcType));
        return false;
    }

    TByteArray Payload;
    if (PayloadSize > 0)
    {
        Payload.resize(PayloadSize);
        std::memcpy(Payload.data(), Data.data() + Offset, PayloadSize);
    }

    MReflectArchive Ar(Payload);
    return ServiceInstance->InvokeSerializedFunction(FuncMeta, Ar);
}

bool TryInvokeServerRpc(MObject* ServiceInstance, uint64 ConnectionId, const TByteArray& Data, ERpcType ExpectedType)
{
    FScopedServerRpcConnectionContext Scope(ConnectionId);
    return TryInvokeServerRpc(ServiceInstance, Data, ExpectedType);
}

const MFunction* FindServerCallFunctionByName(const MClass* TargetClass, const char* FunctionName)
{
    if (!TargetClass || !FunctionName || FunctionName[0] == '\0')
    {
        return nullptr;
    }

    const MFunction* Function = const_cast<MClass*>(TargetClass)->FindFunction(FunctionName);
    return IsServerCallFunction(Function) ? Function : nullptr;
}

const MFunction* FindServerCallFunctionById(const MClass* TargetClass, uint16 FunctionId)
{
    if (!TargetClass || FunctionId == 0)
    {
        return nullptr;
    }

    const MFunction* Function = const_cast<MClass*>(TargetClass)->FindFunctionById(FunctionId);
    return IsServerCallFunction(Function) ? Function : nullptr;
}

bool DispatchServerCall(
    MObject* TargetInstance,
    uint16 FunctionId,
    uint64 CallId,
    const TByteArray& Payload,
    const TSharedPtr<IServerCallResponseTarget>& ResponseTarget)
{
    if (!TargetInstance || FunctionId == 0 || CallId == 0 || !ResponseTarget)
    {
        return false;
    }

    MClass* TargetClass = TargetInstance->GetClass();
    if (!TargetClass)
    {
        (void)ResponseTarget->SendServerCallResponse(
            FunctionId,
            CallId,
            false,
            BuildPayload(FAppError::Make("server_call_missing_class")));
        return false;
    }

    const MFunction* Function = FindServerCallFunctionById(TargetClass, FunctionId);
    if (!Function || !Function->ServerCallHandler)
    {
        (void)ResponseTarget->SendServerCallResponse(
            FunctionId,
            CallId,
            false,
            BuildPayload(FAppError::Make("server_call_missing_handler", std::to_string(FunctionId))));
        LOG_WARN("Server call dispatch failed: class=%s function_id=%u",
                 TargetClass->GetName().c_str(),
                 static_cast<unsigned>(FunctionId));
        return false;
    }

    const SServerCallContext Context{
        FunctionId,
        CallId,
        ResponseTarget,
    };
    FScopedServerCallContext Scope(Context);

    SFutureResult<TByteArray> RespFuture = Function->ServerCallHandler(TargetInstance, Payload);

    if (RespFuture.IsReady())
    {
        // Ready path: dispatch response/error inline.
        TResult<TByteArray, FAppError> Result = RespFuture.GetResult();
        if (Result.IsErr())
        {
            (void)ResponseTarget->SendServerCallResponse(
                FunctionId, CallId, false, BuildPayload(Result.GetError()));
        }
        else
        {
            (void)ResponseTarget->SendServerCallResponse(
                FunctionId, CallId, true, std::move(Result.GetValue()));
        }
        return true;
    }

    // Pending path: continuation goes through ambient MAsyncContext::Post.
    MAsync::MAsyncContext* Ctx = MAsync::MAsyncContext::Current();
    if (!Ctx)
    {
        LOG_ERROR("Pending ServerCall has no MAsyncContext — synthesizing error");
        (void)ResponseTarget->SendServerCallResponse(
            FunctionId, CallId, false,
            BuildPayload(FAppError::Make("async_context_missing")));
        return true;
    }

    RespFuture.Then(
        [Ctx, ResponseTarget, FunctionId, CallId]
        (SFutureResult<TByteArray> F) mutable
        {
            Ctx->Post(
                [F = std::move(F), ResponseTarget, FunctionId, CallId]() mutable
                {
                    TResult<TByteArray, FAppError> Result = F.GetResult();
                    if (Result.IsErr())
                    {
                        (void)ResponseTarget->SendServerCallResponse(
                            FunctionId, CallId, false, BuildPayload(Result.GetError()));
                    }
                    else
                    {
                        (void)ResponseTarget->SendServerCallResponse(
                            FunctionId, CallId, true, std::move(Result.GetValue()));
                    }
                });
        });
    return true;   // true = "we accepted the call"; not "completed synchronously"
}

uint64 RegisterServerCall(
    TFunction<void(const SServerCallResponse&)> Completion,
    double TimeoutSeconds,
    TFunction<bool()> LivenessProbe)
{
    if (!Completion)
    {
        return 0;
    }

    const uint64 CallId = GNextServerCallId.fetch_add(1);
    std::lock_guard<std::mutex> Lock(GServerCallMutex);
    SPendingServerCall Pending;
    Pending.Completion = std::move(Completion);
    Pending.DeadlineSeconds = MTime::GetTimeSeconds() + ((TimeoutSeconds > 0.0) ? TimeoutSeconds : 5.0);
    Pending.LivenessProbe = std::move(LivenessProbe);
    GPendingServerCalls[CallId] = std::move(Pending);
    return CallId;
}

void CancelServerCall(uint64 CallId)
{
    if (CallId == 0)
    {
        return;
    }

    std::lock_guard<std::mutex> Lock(GServerCallMutex);
    GPendingServerCalls.erase(CallId);
}

bool ConsumeServerCall(uint64 CallId, const SServerCallResponse* Response)
{
    TFunction<void(const SServerCallResponse&)> Completion;
    {
        std::lock_guard<std::mutex> Lock(GServerCallMutex);
        auto It = GPendingServerCalls.find(CallId);
        if (It == GPendingServerCalls.end())
        {
            return false;
        }

        Completion = std::move(It->second.Completion);
        GPendingServerCalls.erase(It);
    }

    if (Completion && Response)
    {
        Completion(*Response);
    }

    return true;
}

void PumpServerCallMaintenance()
{
    struct SExpiredServerCall
    {
        TFunction<void(const SServerCallResponse&)> Completion;
        SServerCallResponse Response;
    };

    TVector<SExpiredServerCall> ExpiredCalls;
    const double NowSeconds = MTime::GetTimeSeconds();

    {
        std::lock_guard<std::mutex> Lock(GServerCallMutex);
        for (auto It = GPendingServerCalls.begin(); It != GPendingServerCalls.end();)
        {
            bool bRemove = false;
            FAppError Error;

            if (It->second.LivenessProbe && !It->second.LivenessProbe())
            {
                bRemove = true;
                Error = FAppError::Make("server_call_disconnected");
            }
            else if (It->second.DeadlineSeconds > 0.0 && NowSeconds >= It->second.DeadlineSeconds)
            {
                bRemove = true;
                Error = FAppError::Make("server_call_timeout");
            }

            if (!bRemove)
            {
                ++It;
                continue;
            }

            if (It->second.Completion)
            {
                ExpiredCalls.push_back(SExpiredServerCall{
                    std::move(It->second.Completion),
                    SServerCallResponse{false, BuildPayload(Error)}
                });
            }
            It = GPendingServerCalls.erase(It);
        }
    }

    for (SExpiredServerCall& Expired : ExpiredCalls)
    {
        if (Expired.Completion)
        {
            Expired.Completion(Expired.Response);
        }
    }
}

bool HandleServerCallResponse(const TByteArray& Data)
{
    uint16 FunctionId = 0;
    uint64 CallId = 0;
    bool bSuccess = false;
    uint32 PayloadSize = 0;
    size_t PayloadOffset = 0;
    if (!ParseServerCallResponsePacket(Data, FunctionId, CallId, bSuccess, PayloadSize, PayloadOffset))
    {
        return false;
    }

    TByteArray Payload;
    if (PayloadSize > 0)
    {
        Payload.insert(
            Payload.end(),
            Data.begin() + static_cast<TByteArray::difference_type>(PayloadOffset),
            Data.begin() + static_cast<TByteArray::difference_type>(PayloadOffset + PayloadSize));
    }

    const SServerCallResponse Response{bSuccess, std::move(Payload)};
    return ConsumeServerCall(CallId, &Response);
}

bool DispatchBackendServerCallPacket(
    MObject* ServiceInstance,
    const TSharedPtr<MServerConnection>& Connection,
    const TByteArray& Data)
{
    if (!ServiceInstance || !Connection || Data.empty())
    {
        return false;
    }

    uint16 FunctionId = 0;
    uint64 CallId = 0;
    uint32 PayloadSize = 0;
    size_t PayloadOffset = 0;
    if (!ParseServerCallPacket(Data, FunctionId, CallId, PayloadSize, PayloadOffset))
    {
        return false;
    }

    TByteArray RequestPayload;
    if (PayloadSize > 0)
    {
        RequestPayload.insert(
            RequestPayload.end(),
            Data.begin() + static_cast<TByteArray::difference_type>(PayloadOffset),
            Data.begin() + static_cast<TByteArray::difference_type>(PayloadOffset + PayloadSize));
    }

    const TSharedPtr<IServerCallResponseTarget> ResponseTarget =
        MakeShared<MServerCallResponseTarget>(
            [Connection]() -> bool
            {
                return Connection && Connection->IsConnected();
            },
            [Connection](uint16 ResponseFunctionId, uint64 ResponseCallId, bool bSuccess, const TByteArray& ResponsePayload) -> bool
            {
                TByteArray ResponsePacketPayload;
                if (!BuildServerCallResponsePacket(
                        ResponseFunctionId,
                        ResponseCallId,
                        bSuccess,
                        ResponsePayload,
                        ResponsePacketPayload))
                {
                    return false;
                }

                return SendServerCallResponseMessage(Connection, ResponsePacketPayload);
            });

    return DispatchServerCall(ServiceInstance, FunctionId, CallId, RequestPayload, ResponseTarget);
}

bool DispatchBackendServerCallPacketInbound(
    MObject* ServiceInstance,
    const TSharedPtr<INetConnection>& Connection,
    const TByteArray& Data)
{
    if (!ServiceInstance || !Connection || Data.empty())
    {
        return false;
    }

    uint16 FunctionId = 0;
    uint64 CallId = 0;
    uint32 PayloadSize = 0;
    size_t PayloadOffset = 0;
    if (!ParseServerCallPacket(Data, FunctionId, CallId, PayloadSize, PayloadOffset))
    {
            return false;
    }

    TByteArray RequestPayload;
    if (PayloadSize > 0)
    {
        RequestPayload.insert(
            RequestPayload.end(),
            Data.begin() + static_cast<TByteArray::difference_type>(PayloadOffset),
            Data.begin() + static_cast<TByteArray::difference_type>(PayloadOffset + PayloadSize));
    }

    const TSharedPtr<IServerCallResponseTarget> ResponseTarget =
        MakeShared<MServerCallResponseTarget>(
            [Connection]() -> bool
            {
                return Connection && Connection->IsConnected();
            },
            [Connection](uint16 ResponseFunctionId, uint64 ResponseCallId, bool bSuccess, const TByteArray& ResponsePayload) -> bool
            {
                TByteArray ResponsePacketPayload;
                if (!BuildServerCallResponsePacket(
                        ResponseFunctionId,
                        ResponseCallId,
                        bSuccess,
                        ResponsePayload,
                        ResponsePacketPayload))
                {
                    return false;
                }

                return SendServerCallResponseMessage(Connection, ResponsePacketPayload);
            });

    return DispatchServerCall(ServiceInstance, FunctionId, CallId, RequestPayload, ResponseTarget);
}

bool BuildServerCallPayload(const MFunction* Function, const TByteArray& RequestPayload, TByteArray& OutData)
{
    if (!IsServerCallFunction(Function))
    {
        return false;
    }

    OutData = RequestPayload;
    return true;
}
