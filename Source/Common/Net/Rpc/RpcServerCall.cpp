/**
 * @file RpcServerCall.cpp
 * @brief RpcServerCall 的实现:入站派发、结果回包、挂起调用表的登记/消费/超时清理。
 */

#include "Common/Net/Rpc/RpcServerCall.h"

#include "Common/Runtime/Async/AsyncContext.h"
#include "Common/Runtime/Async/MAsync.h"
#include "Common/Runtime/Log/Log.h"
#include "Common/Runtime/Time.h"

#include <atomic>
#include <cstring>
#include <mutex>

namespace {
    // 两个 thread_local 上下文:只在"当前线程正在处理某个入站包"期间有效,
    // 供调用栈深处的业务代码读取(如判断是哪条连接触发的调用)。
    thread_local uint64             GCurrentServerRpcConnectionId = 0;
    thread_local SServerCallContext GCurrentServerCallContext;

    /// RAII:设置当前连接 id,析构时恢复原值(支持嵌套)。
    class FScopedServerRpcConnectionContext {
        public:
        explicit FScopedServerRpcConnectionContext(uint64 InConnectionId) : PreviousConnectionId(GCurrentServerRpcConnectionId) {
            GCurrentServerRpcConnectionId = InConnectionId;
        }

        ~FScopedServerRpcConnectionContext() {
            GCurrentServerRpcConnectionId = PreviousConnectionId;
        }

        private:
        uint64 PreviousConnectionId = 0;
    };

    /// RAII:压入当前 ServerCall 上下文,析构时恢复原值(支持嵌套派发)。
    class FScopedServerCallContext {
        public:
        explicit FScopedServerCallContext(const SServerCallContext& InContext) : PreviousContext(GCurrentServerCallContext) {
            GCurrentServerCallContext = InContext;
        }

        ~FScopedServerCallContext() {
            GCurrentServerCallContext = PreviousContext;
        }

        private:
        SServerCallContext PreviousContext;
    };

    /// 一条挂起中的调用:完成回调 + 截止时刻 + 可选的连接存活探测。
    struct SPendingServerCall {
        TFunction<void(const SServerCallResponse&)> Completion;
        double                                      DeadlineSeconds = 0.0; ///< 绝对时刻(秒),由 MTime 提供
        TFunction<bool()>                           LivenessProbe;
    };

    // CallId 从 1 开始分配:0 在 wire 上是"未分配"的哨兵,不能用作真实 id。
    std::atomic<uint64>              GNextServerCallId{1};
    std::mutex                       GServerCallMutex; ///< 保护 GPendingServerCalls
    TMap<uint64, SPendingServerCall> GPendingServerCalls;

    /// ServerCall 的判定依据是反射元数据里的 Transport 字段(而非命名约定)。
    bool IsServerCallFunction(const MFunction* Function) {
        return Function && Function->Transport == "ServerCall";
    }
} // namespace

/// @brief 只读头 2 字节的 FunctionId,不做任何校验(长度不足返回 0)。
uint16 PeekServerRpcFunctionId(const TByteArray& Data) {
    if (Data.size() < sizeof(uint16)) {
        return 0;
    }

    uint16 FunctionId = 0;
    std::memcpy(&FunctionId, Data.data(), sizeof(FunctionId));
    return FunctionId;
}

/// @return 当前线程正在处理的连接 id(thread_local);不在处理连接上时为 0。
uint64 GetCurrentServerRpcConnectionId() {
    return GCurrentServerRpcConnectionId;
}

/// @return 当前线程的 ServerCall 上下文快照(可按值留存,供异步回包使用)。
SServerCallContext CaptureCurrentServerCallContext() {
    return GCurrentServerCallContext;
}

// 异步回包的唯一出口:上下文无效或响应目标不可发送时返回 false,不抛异常。
bool SendDeferredServerCallResponse(const SServerCallContext& Context, bool bSuccess, const TByteArray& Payload) {
    if (!Context.IsValid()) {
        return false;
    }

    if (!Context.ResponseTarget->CanSendServerCallResponse()) {
        return false;
    }

    return Context.ResponseTarget->SendServerCallResponse(Context.FunctionId, Context.CallId, bSuccess, Payload);
}

bool SendDeferredServerCallSuccessResponse(const SServerCallContext& Context, const TByteArray& Payload) {
    return SendDeferredServerCallResponse(Context, true, Payload);
}

bool SendDeferredServerCallErrorResponse(const SServerCallContext& Context, const FAppError& Error) {
    return SendDeferredServerCallResponse(Context, false, BuildPayload(Error));
}

// 包体格式:[FunctionId:2B][PayloadSize:4B][Payload:N](与 BuildServerRpcPayload 一致)。
// 全部长度校验都在调用前完成,避免把畸形包交给反射层。
bool TryInvokeServerRpc(MObject* ServiceInstance, const TByteArray& Data, ERpcType ExpectedType) {
    if (!ServiceInstance) {
        return false;
    }

    if (Data.size() < sizeof(uint16) + sizeof(uint32)) {
        LOG_WARN("TryInvokeServerRpc: packet too small (size=%llu)", static_cast<unsigned long long>(Data.size()));
        return false;
    }

    size_t Offset      = 0;
    uint16 FunctionId  = 0;
    uint32 PayloadSize = 0;

    std::memcpy(&FunctionId, Data.data() + Offset, sizeof(FunctionId));
    Offset += sizeof(FunctionId);

    std::memcpy(&PayloadSize, Data.data() + Offset, sizeof(PayloadSize));
    Offset += sizeof(PayloadSize);

    if (Offset + PayloadSize > Data.size()) {
        LOG_WARN("TryInvokeServerRpc: payload out of range (size=%llu, payload=%u)", static_cast<unsigned long long>(Data.size()), static_cast<unsigned>(PayloadSize));
        return false;
    }

    MClass* ServiceClass = ServiceInstance->GetClass();
    if (!ServiceClass) {
        LOG_ERROR("TryInvokeServerRpc: ServiceInstance has no class");
        return false;
    }

    MFunction* FuncMeta = ServiceClass->FindFunctionById(FunctionId);
    if (!FuncMeta) {
        LOG_WARN("TryInvokeServerRpc: unknown FunctionId=%u", static_cast<unsigned>(FunctionId));
        return false;
    }

    if (FuncMeta->RpcType != ExpectedType) {
        LOG_WARN("TryInvokeServerRpc: RpcType mismatch for FunctionId=%u (expected=%d, actual=%d)", static_cast<unsigned>(FunctionId), static_cast<int>(ExpectedType), static_cast<int>(FuncMeta->RpcType));
        return false;
    }

    TByteArray Payload;
    if (PayloadSize > 0) {
        Payload.resize(PayloadSize);
        std::memcpy(Payload.data(), Data.data() + Offset, PayloadSize);
    }

    MReflectArchive Ar(Payload);
    return ServiceInstance->InvokeSerializedFunction(FuncMeta, Ar);
}

/// @brief 带连接上下文的入口:设置 thread_local 后转发给主实现。
bool TryInvokeServerRpc(MObject* ServiceInstance, uint64 ConnectionId, const TByteArray& Data, ERpcType ExpectedType) {
    FScopedServerRpcConnectionContext Scope(ConnectionId);
    return TryInvokeServerRpc(ServiceInstance, Data, ExpectedType);
}

/// @brief 按名字查,并把非 ServerCall 的函数过滤掉。
const MFunction* FindServerCallFunctionByName(const MClass* TargetClass, const char* FunctionName) {
    if (!TargetClass || !FunctionName || FunctionName[0] == '\0') {
        return nullptr;
    }

    const MFunction* Function = const_cast<MClass*>(TargetClass)->FindFunction(FunctionName);
    return IsServerCallFunction(Function) ? Function : nullptr;
}

/// @brief 按 id 查,并把非 ServerCall 的函数过滤掉。
const MFunction* FindServerCallFunctionById(const MClass* TargetClass, uint16 FunctionId) {
    if (!TargetClass || FunctionId == 0) {
        return nullptr;
    }

    const MFunction* Function = const_cast<MClass*>(TargetClass)->FindFunctionById(FunctionId);
    return IsServerCallFunction(Function) ? Function : nullptr;
}

// 返回 true 只代表"调用已受理":同步 handler 在这里就回完包;异步 handler 只挂续体,
// 回包发生在 future 兑现之后。返回 false 才是真正的失败。
bool DispatchServerCall(MObject* TargetInstance, uint16 FunctionId, uint64 CallId, const TByteArray& Payload, const TSharedPtr<IServerCallResponseTarget>& ResponseTarget) {
    if (!TargetInstance || FunctionId == 0 || CallId == 0 || !ResponseTarget) {
        return false;
    }

    MClass* TargetClass = TargetInstance->GetClass();
    if (!TargetClass) {
        // 连类都拿不到:尽量回一个错误让对端不必等到超时。
        (void)ResponseTarget->SendServerCallResponse(FunctionId, CallId, false, BuildPayload(FAppError::Make("server_call_missing_class")));
        return false;
    }

    const MFunction* Function = FindServerCallFunctionById(TargetClass, FunctionId);
    if (!Function || !Function->ServerCallHandler) {
        (void)ResponseTarget->SendServerCallResponse(FunctionId, CallId, false, BuildPayload(FAppError::Make("server_call_missing_handler", std::to_string(FunctionId))));
        LOG_WARN("Server call dispatch failed: class=%s function_id=%u", TargetClass->GetName().c_str(), static_cast<unsigned>(FunctionId));
        return false;
    }

    // 压入上下文:handler(及其调用的下游)可经 CaptureCurrentServerCallContext 取到,
    // 用于稍后异步回包。作用域结束时自动恢复。
    const SServerCallContext Context{
        FunctionId,
        CallId,
        ResponseTarget,
    };
    FScopedServerCallContext Scope(Context);

    SFutureResult<TByteArray> RespFuture = Function->ServerCallHandler(TargetInstance, Payload);

    if (RespFuture.IsReady()) {
        // 同步完成路径:直接就地回包(常见于非 Async 的 ServerCall)。
        TResult<TByteArray, FAppError> Result = RespFuture.GetResult();
        if (Result.IsErr()) {
            (void)ResponseTarget->SendServerCallResponse(FunctionId, CallId, false, BuildPayload(Result.GetError()));
        } else {
            (void)ResponseTarget->SendServerCallResponse(FunctionId, CallId, true, std::move(Result.GetValue()));
        }
        return true;
    }

    // 异步路径:续体经 ambient MAsyncContext::Post 回到当前线程执行,而不是就地执行 ——
    // 避免在 I/O 线程上跑业务代码。
    MAsync::MAsyncContext* Ctx = MAsync::MAsyncContext::Current();
    if (!Ctx) {
        LOG_ERROR("Pending ServerCall has no MAsyncContext — synthesizing error");
        (void)ResponseTarget->SendServerCallResponse(FunctionId, CallId, false, BuildPayload(FAppError::Make("async_context_missing")));
        return true;
    }

    RespFuture.Then([Ctx, ResponseTarget, FunctionId, CallId](SFutureResult<TByteArray> F) mutable {
        Ctx->Post([F = std::move(F), ResponseTarget, FunctionId, CallId]() mutable {
            TResult<TByteArray, FAppError> Result = F.GetResult();
            if (Result.IsErr()) {
                (void)ResponseTarget->SendServerCallResponse(FunctionId, CallId, false, BuildPayload(Result.GetError()));
            } else {
                (void)ResponseTarget->SendServerCallResponse(FunctionId, CallId, true, std::move(Result.GetValue()));
            }
        });
    });
    return true; // 返回 true 仅表示"调用已受理",不代表"已同步执行完成"
}

// CallId 由原子计数器分配,保证并发登记不冲突;超时传 0/负数时回退到默认 5 秒。
uint64 RegisterServerCall(TFunction<void(const SServerCallResponse&)> Completion, double TimeoutSeconds, TFunction<bool()> LivenessProbe) {
    if (!Completion) {
        return 0;
    }

    const uint64                CallId = GNextServerCallId.fetch_add(1);
    std::lock_guard<std::mutex> Lock(GServerCallMutex);
    SPendingServerCall          Pending;
    Pending.Completion          = std::move(Completion);
    Pending.DeadlineSeconds     = MTime::GetTimeSeconds() + ((TimeoutSeconds > 0.0) ? TimeoutSeconds : 5.0);
    Pending.LivenessProbe       = std::move(LivenessProbe);
    GPendingServerCalls[CallId] = std::move(Pending);
    return CallId;
}

void CancelServerCall(uint64 CallId) {
    if (CallId == 0) {
        return;
    }

    std::lock_guard<std::mutex> Lock(GServerCallMutex);
    GPendingServerCalls.erase(CallId);
}

// 先在锁内摘除条目,再到锁外触发回调 —— 回调里可以安全地再次发起 RPC,不会自锁。
bool ConsumeServerCall(uint64 CallId, const SServerCallResponse* Response) {
    TFunction<void(const SServerCallResponse&)> Completion;
    {
        std::lock_guard<std::mutex> Lock(GServerCallMutex);
        auto                        It = GPendingServerCalls.find(CallId);
        if (It == GPendingServerCalls.end()) {
            return false;
        }

        Completion = std::move(It->second.Completion);
        GPendingServerCalls.erase(It);
    }

    if (Completion && Response) {
        Completion(*Response);
    }

    return true;
}

// 两类清理:连接已断(LivenessProbe 为假)→ server_call_disconnected;已过截止时刻
// → server_call_timeout。过期的回调同样收集到锁外再统一触发。
void PumpServerCallMaintenance() {
    struct SExpiredServerCall {
        TFunction<void(const SServerCallResponse&)> Completion;
        SServerCallResponse                         Response;
    };

    TVector<SExpiredServerCall> ExpiredCalls;
    const double                NowSeconds = MTime::GetTimeSeconds();

    {
        std::lock_guard<std::mutex> Lock(GServerCallMutex);
        for (auto It = GPendingServerCalls.begin(); It != GPendingServerCalls.end();) {
            bool      bRemove = false;
            FAppError Error;

            if (It->second.LivenessProbe && !It->second.LivenessProbe()) {
                bRemove = true;
                Error   = FAppError::Make("server_call_disconnected");
            } else if (It->second.DeadlineSeconds > 0.0 && NowSeconds >= It->second.DeadlineSeconds) {
                bRemove = true;
                Error   = FAppError::Make("server_call_timeout");
            }

            if (!bRemove) {
                ++It;
                continue;
            }

            if (It->second.Completion) {
                ExpiredCalls.push_back(SExpiredServerCall{std::move(It->second.Completion), SServerCallResponse{false, BuildPayload(Error)}});
            }
            It = GPendingServerCalls.erase(It);
        }
    }

    for (SExpiredServerCall& Expired : ExpiredCalls) {
        if (Expired.Completion) {
            Expired.Completion(Expired.Response);
        }
    }
}

// MT_FunctionResponse 的入口:解析头部 → 拷出负载 → 交给 ConsumeServerCall 按 CallId 兑现。
bool HandleServerCallResponse(const TByteArray& Data) {
    uint16 FunctionId    = 0;
    uint64 CallId        = 0;
    bool   bSuccess      = false;
    uint32 PayloadSize   = 0;
    size_t PayloadOffset = 0;
    if (!ParseServerCallResponsePacket(Data, FunctionId, CallId, bSuccess, PayloadSize, PayloadOffset)) {
        return false;
    }

    TByteArray Payload;
    if (PayloadSize > 0) {
        Payload.insert(Payload.end(), Data.begin() + static_cast<TByteArray::difference_type>(PayloadOffset), Data.begin() + static_cast<TByteArray::difference_type>(PayloadOffset + PayloadSize));
    }

    const SServerCallResponse Response{bSuccess, std::move(Payload)};
    return ConsumeServerCall(CallId, &Response);
}

/// @brief 入站 MT_FunctionCall(MServerConnection 版):解析 → 建回包通道 → 派发。
bool DispatchBackendServerCallPacket(MObject* ServiceInstance, const TSharedPtr<MServerConnection>& Connection, const TByteArray& Data) {
    if (!ServiceInstance || !Connection || Data.empty()) {
        return false;
    }

    uint16 FunctionId    = 0;
    uint64 CallId        = 0;
    uint32 PayloadSize   = 0;
    size_t PayloadOffset = 0;
    if (!ParseServerCallPacket(Data, FunctionId, CallId, PayloadSize, PayloadOffset)) {
        return false;
    }

    TByteArray RequestPayload;
    if (PayloadSize > 0) {
        RequestPayload.insert(RequestPayload.end(), Data.begin() + static_cast<TByteArray::difference_type>(PayloadOffset), Data.begin() + static_cast<TByteArray::difference_type>(PayloadOffset + PayloadSize));
    }

    const TSharedPtr<IServerCallResponseTarget> ResponseTarget = MakeShared<MServerCallResponseTarget>([Connection]() -> bool { return Connection && Connection->IsConnected(); },
                                                                                                       [Connection](uint16 ResponseFunctionId, uint64 ResponseCallId, bool bSuccess, const TByteArray& ResponsePayload) -> bool {
                                                                                                           TByteArray ResponsePacketPayload;
                                                                                                           if (!BuildServerCallResponsePacket(ResponseFunctionId, ResponseCallId, bSuccess, ResponsePayload, ResponsePacketPayload)) {
                                                                                                               return false;
                                                                                                           }

                                                                                                           return SendServerCallResponseMessage(Connection, ResponsePacketPayload);
                                                                                                       });

    return DispatchServerCall(ServiceInstance, FunctionId, CallId, RequestPayload, ResponseTarget);
}

/// @brief 入站 MT_FunctionCall(通用连接版):与上一版逻辑相同,只是连接类型不同。
bool DispatchBackendServerCallPacketInbound(MObject* ServiceInstance, const TSharedPtr<INetConnection>& Connection, const TByteArray& Data) {
    if (!ServiceInstance || !Connection || Data.empty()) {
        return false;
    }

    uint16 FunctionId    = 0;
    uint64 CallId        = 0;
    uint32 PayloadSize   = 0;
    size_t PayloadOffset = 0;
    if (!ParseServerCallPacket(Data, FunctionId, CallId, PayloadSize, PayloadOffset)) {
        return false;
    }

    TByteArray RequestPayload;
    if (PayloadSize > 0) {
        RequestPayload.insert(RequestPayload.end(), Data.begin() + static_cast<TByteArray::difference_type>(PayloadOffset), Data.begin() + static_cast<TByteArray::difference_type>(PayloadOffset + PayloadSize));
    }

    const TSharedPtr<IServerCallResponseTarget> ResponseTarget = MakeShared<MServerCallResponseTarget>([Connection]() -> bool { return Connection && Connection->IsConnected(); },
                                                                                                       [Connection](uint16 ResponseFunctionId, uint64 ResponseCallId, bool bSuccess, const TByteArray& ResponsePayload) -> bool {
                                                                                                           TByteArray ResponsePacketPayload;
                                                                                                           if (!BuildServerCallResponsePacket(ResponseFunctionId, ResponseCallId, bSuccess, ResponsePayload, ResponsePacketPayload)) {
                                                                                                               return false;
                                                                                                           }

                                                                                                           return SendServerCallResponseMessage(Connection, ResponsePacketPayload);
                                                                                                       });

    return DispatchServerCall(ServiceInstance, FunctionId, CallId, RequestPayload, ResponseTarget);
}

// 只做 Transport 校验后原样拷贝:头部由 BuildServerCallPacket 负责拼。
bool BuildServerCallPayload(const MFunction* Function, const TByteArray& RequestPayload, TByteArray& OutData) {
    if (!IsServerCallFunction(Function)) {
        return false;
    }

    OutData = RequestPayload;
    return true;
}
