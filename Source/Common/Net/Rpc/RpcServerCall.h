/**
 * @file RpcServerCall.h
 * @brief RpcServerCall - 服务器间"请求-响应"式反射 RPC(ServerCall)的收发两端。
 *
 * @detail 一条 ServerCall 的完整生命周期:
 *   1. 发起侧 CallServerFunction(...):查反射表拿 FunctionId → 分配 CallId 并向
 *      RegisterServerCall 登记完成回调 → BuildServerCallPacket → 发 MT_FunctionCall;
 *   2. 接收侧:解析包 → DispatchServerCall 反射调用目标方法 → 把结果序列化,
 *      经 IServerCallResponseTarget 以 MT_FunctionResponse 回包;handler 若返回未 ready 的
 *      future(Async 形态),回包会推迟到 future 兑现后再发;
 *   3. 发起侧:HandleServerCallResponse 解析 MT_FunctionResponse → ConsumeServerCall
 *      按 CallId 取出回调并触发。
 *
 *   兜底:PumpServerCallMaintenance 由主循环每帧调用,负责把"连接已断"或"已超时"的挂起
 *   调用以错误回调结清(错误码 server_call_disconnected / server_call_timeout)。
 *
 *   线程模型:挂起调用表(GPendingServerCalls)由互斥量保护;完成回调一律在锁外执行,
 *   因此回调里可以安全地再次发起 RPC。thread_local 的当前调用上下文只对同线程可见。
 */

#pragma once

#include "Common/Net/Rpc/RpcErrors.h"
#include "Common/Net/Rpc/RpcManifest.h"

#include <utility>

/**
 * @brief IServerCallResponseTarget - 一次 ServerCall 的"把响应发回请求方"的能力抽象。
 */
class IServerCallResponseTarget {
    public:
    virtual ~IServerCallResponseTarget() = default;

    /// @return 当前是否还能发响应(通常是底层连接仍可写)。
    virtual bool CanSendServerCallResponse() const = 0;

    /**
     * @brief SendServerCallResponse - 发一条响应回去。
     * @param FunctionId 被调用的函数 id
     * @param CallId     请求携带的调用 id
     * @param bSuccess   true 时 Payload 是返回值,false 时是序列化的 FAppError
     * @param Payload    已序列化的返回值或错误
     * @return true 已入队
     */
    virtual bool SendServerCallResponse(uint16 FunctionId, uint64 CallId, bool bSuccess, const TByteArray& Payload) = 0;
};

/// 一次进行中的 ServerCall 的上下文,供 handler 在稍后(Async)异步回包时使用。
struct SServerCallContext {
    uint16                                FunctionId = 0;
    uint64                                CallId     = 0;
    TSharedPtr<IServerCallResponseTarget> ResponseTarget;

    /// @return 三个字段都非空才算有效;无效的上下文不能用于回包。
    bool IsValid() const {
        return FunctionId != 0 && CallId != 0 && ResponseTarget != nullptr;
    }
};

/// 一条 ServerCall 响应已解析出的结果。
struct SServerCallResponse {
    bool       bSuccess = false; ///< true:Payload 是返回值;false:Payload 是序列化的 FAppError
    TByteArray Payload;          ///< 已反序列化前的原始字节
};

/**
 * @brief MServerCallResponseTarget - 用两个回调拼出来的 IServerCallResponseTarget 实现。
 *
 * @detail 典型用法见 DispatchBackendServerCallPacket*:把一个连接包成
 *   "CanSend = 连接是否可用" + "Send = 拼响应包并发送"。
 */
class MServerCallResponseTarget final : public IServerCallResponseTarget {
    public:
    MServerCallResponseTarget(TFunction<bool()> InCanSend, TFunction<bool(uint16, uint64, bool, const TByteArray&)> InSend) : CanSendCallback(std::move(InCanSend)), SendCallback(std::move(InSend)) {
    }

    bool CanSendServerCallResponse() const override {
        return CanSendCallback ? CanSendCallback() : false;
    }

    bool SendServerCallResponse(uint16 FunctionId, uint64 CallId, bool bSuccess, const TByteArray& Payload) override {
        // 先问"还能不能发",避免往一个已断的连接写数据;两个回调任一为空都按失败处理。
        if (!CanSendServerCallResponse()) {
            return false;
        }
        return SendCallback ? SendCallback(FunctionId, CallId, bSuccess, Payload) : false;
    }

    private:
    TFunction<bool()>                                        CanSendCallback; ///< 可发送性探测(空 = 恒 false)
    TFunction<bool(uint16, uint64, bool, const TByteArray&)> SendCallback;    ///< 实际发送(空 = 恒 false)
};

/**
 * @brief TryInvokeServerRpc - 反射调用 ServiceInstance 上的一个函数(同步、无回包)。
 * @param ServiceInstance 目标对象(须为 MObject 派生)
 * @param Data            包体 [FunctionId:2B][PayloadSize:4B][Payload:N](与 BuildServerRpcPayload 一致)
 * @param ExpectedType    期望的 RpcType;与反射元数据不符时拒绝调用
 * @return true 找到函数且已调用;false 对象为空 / 包太短 / 负载越界 / FunctionId 未知 / RpcType 不匹配
 *
 * @detail 只负责"调用",不产生任何响应(响应语义属于 ServerCall 那一套)。
 *   失败原因都会打 WARN/ERROR 日志,便于定位协议不匹配。
 */
bool TryInvokeServerRpc(MObject* ServiceInstance, const TByteArray& Data, ERpcType ExpectedType);

/**
 * @brief TryInvokeServerRpc - 同上,但先把 ConnectionId 设为当前线程的连接上下文。
 * @param ConnectionId 当前正在处理的连接 id,供调用栈深处的 GetCurrentServerRpcConnectionId() 读取
 *
 * @detail 用 RAII 作用域设置 thread_local,函数返回时自动恢复原值。
 */
bool TryInvokeServerRpc(MObject* ServiceInstance, uint64 ConnectionId, const TByteArray& Data, ERpcType ExpectedType);

/**
 * @brief PeekServerRpcFunctionId - 只从包体头部读出 FunctionId,不做任何校验。
 * @param Data 包体
 * @return 头 2 字节表示的 FunctionId;长度不足 2 字节时返回 0
 *
 * @detail 用于"先看是哪个函数,再决定要不要分发"的场景,不会推进任何解析状态。
 */
uint16 PeekServerRpcFunctionId(const TByteArray& Data);

/**
 * @brief DispatchBackendServerCallPacket - 处理来自 MServerConnection 的入站 MT_FunctionCall。
 * @param ServiceInstance 被调用的服务对象(决定哪个 Class 的方法被调用,通常是 self)
 * @param Connection      入站连接;响应按原路返回给它
 * @param Data            完整包体(不含包类型字节)
 * @return true 已受理该调用;false 参数为空 / 解析失败 / 派发失败
 *
 * @detail 流程:ParseServerCallPacket → 把 Connection 包成 MServerCallResponseTarget →
 *   DispatchServerCall 按 FunctionId 调用并回包。ServiceInstance 必须继承 MObject。
 */
bool DispatchBackendServerCallPacket(MObject* ServiceInstance, const TSharedPtr<MServerConnection>& Connection, const TByteArray& Data);

/**
 * @brief DispatchBackendServerCallPacketInbound - 同上,但连接形态是通用 INetConnection。
 * @param ServiceInstance 被调用的服务对象
 * @param Connection      入站连接(Gateway ↔ Echo 等 MTcpConnection 直连场景)
 * @param Data            完整包体
 * @return true 已受理;false 参数为空 / 解析失败 / 派发失败
 *
 * @detail 与 MServerConnection 版同语义,只是回包走 INetConnection 的发送接口。
 */
bool DispatchBackendServerCallPacketInbound(MObject* ServiceInstance, const TSharedPtr<INetConnection>& Connection, const TByteArray& Data);

/// @return 当前线程正在处理的服务器间连接 id(thread_local);不在处理连接上时为 0。
uint64 GetCurrentServerRpcConnectionId();

/**
 * @brief FindServerCallFunctionByName - 按名字找 ServerCall 函数。
 * @param TargetClass  目标类
 * @param FunctionName 函数名
 * @return 命中的 MFunction;不存在、为空、或该函数的 Transport 不是 "ServerCall" 时返回 nullptr
 */
const MFunction* FindServerCallFunctionByName(const MClass* TargetClass, const char* FunctionName);

/**
 * @brief FindServerCallFunctionById - 按 id 找 ServerCall 函数。
 * @param TargetClass 目标类
 * @param FunctionId  函数 id;0 视为无效
 * @return 命中的 MFunction;不存在、id 为 0、或 Transport 不是 "ServerCall" 时返回 nullptr
 */
const MFunction* FindServerCallFunctionById(const MClass* TargetClass, uint16 FunctionId);

/// @return 当前线程正在处理的 ServerCall 上下文(thread_local);不在 handler 内时是无效上下文。
SServerCallContext CaptureCurrentServerCallContext();

/**
 * @brief SendDeferredServerCallResponse - 用先前捕获的上下文异步回包。
 * @param Context   CaptureCurrentServerCallContext 拿到的上下文
 * @param bSuccess  true 回成功,false 回错误
 * @param Payload   已序列化的返回值或 FAppError
 * @return true 已入队;false 上下文无效,或响应目标当前不可发送
 */
bool SendDeferredServerCallResponse(const SServerCallContext& Context, bool bSuccess, const TByteArray& Payload);

/// @brief SendDeferredServerCallResponse 的成功语法糖。
bool SendDeferredServerCallSuccessResponse(const SServerCallContext& Context, const TByteArray& Payload);

/// @brief SendDeferredServerCallResponse 的错误语法糖,内部会 BuildPayload(Error)。
bool SendDeferredServerCallErrorResponse(const SServerCallContext& Context, const FAppError& Error);

/**
 * @brief DispatchServerCall - 反射调用目标方法并把结果回给请求方。
 * @param TargetInstance 被调用对象
 * @param FunctionId     目标函数 id;为 0 直接失败
 * @param CallId         调用 id;为 0 直接失败
 * @param Payload        请求负载
 * @param ResponseTarget 回包通道
 * @return true 表示"调用已受理"(handler 同步完成,或已挂到异步续体上);
 *   false 表示参数非法 / 找不到函数 / 没有 handler / 缺少 MAsyncContext
 *
 * @detail 返回值语义很重要:true ≠ "已完成"。handler 返回未 ready 的 future 时,回包会推迟到
 *   future 兑现,并经 ambient MAsyncContext::Post 回到当前线程执行;此时若没有 ambient
 *   context,会立刻回一个 async_context_missing 错误并返回 true。
 *   调用前会把上下文压入 thread_local,handler 内可用 CaptureCurrentServerCallContext 取到。
 */
bool DispatchServerCall(MObject* TargetInstance, uint16 FunctionId, uint64 CallId, const TByteArray& Payload, const TSharedPtr<IServerCallResponseTarget>& ResponseTarget);

/**
 * @brief HandleServerCallResponse - 处理入站的 MT_FunctionResponse。
 * @param Data 完整包体(不含包类型字节)
 * @return true 解析成功且 CallId 命中了一个挂起调用(回调已触发);false 解析失败或 CallId 未知
 *
 * @detail 负载会被拷贝一份再交给回调。CallId 未知(例如对端回得太晚、已被超时清掉)时静默返回 false。
 */
bool HandleServerCallResponse(const TByteArray& Data, const void* SourceConnectionIdentity = nullptr);

/**
 * @brief RegisterServerCall - 登记一次待响应的调用。
 * @param Completion     响应到达(或超时/断线)时调用;为空则直接返回 0
 * @param TimeoutSeconds 超时秒数;<= 0 时回退为默认 5.0
 * @param LivenessProbe  可选的存活探测,返回 false 时立刻以 server_call_disconnected 结清
 * @return 新分配的 CallId(从 1 开始自增,0 永不使用);失败返回 0
 */
uint64 RegisterServerCall(TFunction<void(const SServerCallResponse&)> Completion, double TimeoutSeconds = 5.0, TFunction<bool()> LivenessProbe = {}, uint16 ExpectedFunctionId = 0, const void* ExpectedConnectionIdentity = nullptr);

/**
 * @brief CancelServerCall - 撤销一个挂起调用(不再触发其回调)。
 * @param CallId 调用 id;为 0 或不存在时为空操作
 *
 * @detail 发送失败时调用方必须用它撤销登记,否则会白白等到超时。
 */
void CancelServerCall(uint64 CallId);

/**
 * @brief ConsumeServerCall - 取出并移除一个挂起调用,然后触发其回调。
 * @param CallId   调用 id
 * @param Response 响应;为 nullptr 时只移除、不回调
 * @return true 命中并已消费;false CallId 不存在(可能已被消费或已被超时清理)
 *
 * @detail 取出即删除,因此同一 CallId 的回调最多只会被触发一次。回调在锁外执行,
 *   允许在回调里安全地发起新的 RPC。
 */
bool ConsumeServerCall(uint64 CallId, uint16 FunctionId, const void* SourceConnectionIdentity, const SServerCallResponse* Response);

/**
 * @brief PumpServerCallMaintenance - 结清已断线/已超时的挂起调用(主循环每帧调用)。
 *
 * @detail 判定顺序:先看 LivenessProbe(失败 → server_call_disconnected),再看超时
 *   (→ server_call_timeout)。所有回调都收集起来在释放锁之后再触发,避免回调重入造成死锁。
 */
void PumpServerCallMaintenance();

/**
 * @brief BuildServerCallPayload - 把已序列化的请求包装成 ServerCall 负载。
 * @param Function        目标函数;必须存在且 Transport == "ServerCall"
 * @param RequestPayload  已序列化的请求体
 * @param OutData         输出负载
 * @return true 成功;false Function 不是 ServerCall 函数
 *
 * @detail 当前实现就是把请求体原样拷过去(不加头部)—— 头部由 BuildServerCallPacket 负责。
 */
bool BuildServerCallPayload(const MFunction* Function, const TByteArray& RequestPayload, TByteArray& OutData);

/**
 * @brief BuildServerCallLivenessProbe - 用弱引用探测 MServerConnection 是否仍处于连接状态。
 * @param Connection 目标连接
 * @return 探测回调;连接已被销毁或底层传输已断开时返回 false
 *
 * @detail 持弱引用,因此探测回调本身不会延长连接对象的生命周期。
 */
inline TFunction<bool()> BuildServerCallLivenessProbe(const TSharedPtr<MServerConnection>& Connection) {
    TWeakPtr<MServerConnection> WeakConnection = Connection;
    return [WeakConnection]() -> bool {
        const TSharedPtr<MServerConnection> Locked = WeakConnection.lock();
        // 握手本身发生在 TCP 已通、尚未 Authenticated 的窗口；此时不能把
        // 对应 ServerCall 误判成断线。
        return Locked && Locked->IsTransportConnected();
    };
}

/// @brief BuildServerCallLivenessProbe - INetConnection 版,语义同上。
inline TFunction<bool()> BuildServerCallLivenessProbe(const TSharedPtr<INetConnection>& Connection) {
    TWeakPtr<INetConnection> WeakConnection = Connection;
    return [WeakConnection]() -> bool {
        const TSharedPtr<INetConnection> Locked = WeakConnection.lock();
        return Locked && Locked->IsConnected();
    };
}

/// @brief BuildServerCallLivenessProbe - 兜底重载:对无法探测的连接形态返回空回调(不检测存活)。
template <typename TConnection> inline TFunction<bool()> BuildServerCallLivenessProbe(const TConnection&) {
    return {};
}

/// @brief 用 shared_ptr 所持对象的稳定地址标识一条 RPC 连接；非 shared_ptr 连接不做来源校验。
template <typename TConnection> inline const void* GetServerCallConnectionIdentity(const TSharedPtr<TConnection>& Connection) {
    return Connection.get();
}

template <typename TConnection> inline const void* GetServerCallConnectionIdentity(const TConnection&) {
    return nullptr;
}

// ============================ CallServerFunction:发起侧入口(4 个重载) ============================
// 4 个重载的区别只是"怎么定位目标函数":
//   1. 直接给 MFunction*  —— 调用方已自己查过表,避免重复查找;
//   2. 给 MClass* + 函数名 —— 内部按名字查;
//   3. 给 EServerType + 函数名 —— 先用 FindRpcEndpoint 反查类名,再退到约定类名;
//   4. 给类名 + 函数名 —— 用 MGET_STABLE_RPC_FUNCTION_ID 直接算 id,不查反射表。
// 全部返回 future:连接不可用、函数缺失、发送失败都会立即兑现成 Err(见各定义处的错误码)。

/**
 * @brief CallServerFunction - 已持有 MFunction* 的重载。
 * @param Connection          目标连接(引用或 shared_ptr;内部会转发给 SendServerCallMessage)
 * @param Function            目标函数元数据;为空时以 server_call_missing_function 立即失败
 * @param Request             请求体
 * @param MissingFunctionName 失败时写进错误消息的函数名(仅用于诊断)
 * @return 结果 future
 */
template <typename TResponse, typename TRequest, typename TConnection>
inline MFuture<TResult<TResponse, FAppError>> CallServerFunction(TConnection&& Connection, const MFunction* Function, const TRequest& Request, const char* MissingFunctionName = nullptr);

/// @brief CallServerFunction - 按 (MClass*, 函数名) 定位。
template <typename TResponse, typename TRequest, typename TConnection> inline MFuture<TResult<TResponse, FAppError>> CallServerFunction(TConnection&& Connection, const MClass* TargetClass, const char* FunctionName, const TRequest& Request);

/**
 * @brief CallServerFunction - 只知 ServerType + 函数名时定位。
 * @return 找不到端点类名时以 server_call_missing_function 立即失败,
 *   并调用 ReportUnsupportedRpcEndpoint 记一次限频告警
 */
template <typename TResponse, typename TRequest, typename TConnection>
inline MFuture<TResult<TResponse, FAppError>> CallServerFunction(TConnection&& Connection, EServerType TargetServerType, const char* FunctionName, const TRequest& Request);

/// @brief CallServerFunction - 按类名 + 函数名用稳定 id 定位(不走反射表查找)。
template <typename TResponse, typename TRequest, typename TConnection>
inline MFuture<TResult<TResponse, FAppError>> CallServerFunction(TConnection&& Connection, const char* TargetClassName, const char* FunctionName, const TRequest& Request);

/// @brief BuildServerCallPayload - 便捷重载:内部先 BuildPayload(Request) 再包装。
template <typename TRequest> inline bool BuildServerCallPayload(const MFunction* Function, const TRequest& Request, TByteArray& OutData) {
    if (!Function) {
        return false;
    }

    return BuildServerCallPayload(Function, BuildPayload(Request), OutData);
}

/// @brief BuildServerCallPayloadByName - 按类名+函数名查表后包装;查不到返回 false。
template <typename TRequest> inline bool BuildServerCallPayloadByName(const MClass* TargetClass, const char* FunctionName, const TRequest& Request, TByteArray& OutData) {
    const MFunction* Function = FindServerCallFunctionByName(TargetClass, FunctionName);
    if (!Function) {
        return false;
    }

    return BuildServerCallPayload(Function, Request, OutData);
}

// 主实现:所有重载最终都汇聚到这里。
// 顺序:校验 Function → 序列化请求 → RegisterServerCall 登记完成回调(含 5 秒超时 +
// 连接存活探测)→ 拼包 → 发送。任一环节失败都会撤销登记并兑现 Err,保证不泄漏挂起调用。
/**
 * @brief CallServerFunction - 发起一次 ServerCall。
 * @param Connection          目标连接
 * @param Function            目标函数元数据
 * @param Request             请求体
 * @param MissingFunctionName 诊断用的函数名
 * @return future;resolve 值或错误码:
 *   server_call_missing_function / server_call_build_failed / server_call_register_failed /
 *   server_call_packet_build_failed / server_call_send_failed /
 *   server_call_response_parse_failed / server_call_error_parse_failed,
 *   以及响应带回的业务 FAppError;超时与断线由维护回路产出 server_call_timeout /
 *   server_call_disconnected。
 *
 * @detail 固定使用 5.0 秒超时与连接存活探测。返回的 future 可能长时间 pending,
 *   超时/断线的清理不依赖本函数,而由 PumpServerCallMaintenance 驱动。
 */
template <typename TResponse, typename TRequest, typename TConnection>
inline MFuture<TResult<TResponse, FAppError>> CallServerFunction(TConnection&& Connection, const MFunction* Function, const TRequest& Request, const char* MissingFunctionName) {
    MPromise<TResult<TResponse, FAppError>> Promise;
    MFuture<TResult<TResponse, FAppError>>  Future = Promise.GetFuture();

    if (!Function) {
        Promise.SetValue(MakeRpcErrorResult<TResponse>("server_call_missing_function", MissingFunctionName ? MissingFunctionName : ""));
        return Future;
    }

    TByteArray RequestPacket;
    if (!BuildServerCallPayload(Function, Request, RequestPacket)) {
        Promise.SetValue(MakeRpcErrorResult<TResponse>("server_call_build_failed", Function->Name.c_str()));
        return Future;
    }

    MPromise<TResult<TResponse, FAppError>> CompletionPromise = Promise;
    const uint64                            CallId            = RegisterServerCall(
        [Promise = std::move(CompletionPromise), FunctionNameValue = MString(Function->Name)](const SServerCallResponse& Response) mutable {
            if (Response.bSuccess) {
                TResponse ResponseValue{};
                auto      ParseResult = ParsePayload(Response.Payload, ResponseValue, FunctionNameValue.c_str());
                if (!ParseResult.IsOk()) {
                    Promise.SetValue(MakeRpcErrorResult<TResponse>("server_call_response_parse_failed", ParseResult.GetError().c_str()));
                    return;
                }

                Promise.SetValue(TResult<TResponse, FAppError>::Ok(std::move(ResponseValue)));
                return;
            }

            FAppError Error;
            auto      ParseResult = ParsePayload(Response.Payload, Error, FunctionNameValue.c_str());
            if (!ParseResult.IsOk()) {
                Promise.SetValue(MakeRpcErrorResult<TResponse>("server_call_error_parse_failed", ParseResult.GetError().c_str()));
                return;
            }

            Promise.SetValue(MakeErrorResult<TResponse>(std::move(Error)));
        },
        5.0, BuildServerCallLivenessProbe(Connection), Function->FunctionId, GetServerCallConnectionIdentity(Connection));

    if (CallId == 0) {
        Promise.SetValue(MakeRpcErrorResult<TResponse>("server_call_register_failed", Function->Name.c_str()));
        return Future;
    }

    TByteArray PacketPayload;
    if (!BuildServerCallPacket(Function->FunctionId, CallId, RequestPacket, PacketPayload)) {
        CancelServerCall(CallId);
        Promise.SetValue(MakeRpcErrorResult<TResponse>("server_call_packet_build_failed", Function->Name.c_str()));
        return Future;
    }

    if (!SendServerCallMessage(std::forward<TConnection>(Connection), PacketPayload)) {
        CancelServerCall(CallId);
        Promise.SetValue(MakeRpcErrorResult<TResponse>("server_call_send_failed", Function->Name.c_str()));
    }

    return Future;
}

/// @brief 按 (MClass*, 函数名) 查 ServerCall 函数后转发给主实现。
template <typename TResponse, typename TRequest, typename TConnection>
inline MFuture<TResult<TResponse, FAppError>> CallServerFunction(TConnection&& Connection, const MClass* TargetClass, const char* FunctionName, const TRequest& Request) {
    const MFunction* Function = FindServerCallFunctionByName(TargetClass, FunctionName);
    return CallServerFunction<TResponse>(std::forward<TConnection>(Connection), Function, Request, FunctionName);
}

/// @brief 先按 (ServerType, 函数名) 反查类名,失败则退回该 ServerType 的约定端点类名。
template <typename TResponse, typename TRequest, typename TConnection>
inline MFuture<TResult<TResponse, FAppError>> CallServerFunction(TConnection&& Connection, EServerType TargetServerType, const char* FunctionName, const TRequest& Request) {
    SRpcEndpointBinding Binding;
    const char*         TargetClassName = nullptr;
    if (FindRpcEndpoint(TargetServerType, FunctionName, Binding)) {
        TargetClassName = Binding.ClassName;
    } else {
        TargetClassName = GetServerEndpointClassName(TargetServerType);
        if (!TargetClassName) {
            ReportUnsupportedRpcEndpoint(TargetServerType, FunctionName);
            return MakeRpcErrorFuture<TResponse>("server_call_missing_function", FunctionName ? FunctionName : "");
        }
    }

    return CallServerFunction<TResponse>(std::forward<TConnection>(Connection), TargetClassName, FunctionName, Request);
}

// 不查反射表:直接用编译期/约定一致的稳定 id 定位目标函数。
// 代价是对端必须存在同一个 id(即两侧用同一份声明生成),换来了无需运行时查找。
template <typename TResponse, typename TRequest, typename TConnection>
inline MFuture<TResult<TResponse, FAppError>> CallServerFunction(TConnection&& Connection, const char* TargetClassName, const char* FunctionName, const TRequest& Request) {
    if (!TargetClassName || !TargetClassName[0] || !FunctionName || !FunctionName[0]) {
        return MakeRpcErrorFuture<TResponse>("server_call_missing_function", FunctionName ? FunctionName : "");
    }

    const uint16 FunctionId = MGET_STABLE_RPC_FUNCTION_ID(TargetClassName, FunctionName);
    if (FunctionId == 0) {
        return MakeRpcErrorFuture<TResponse>("server_call_missing_function", FunctionName);
    }

    MPromise<TResult<TResponse, FAppError>> Promise;
    MFuture<TResult<TResponse, FAppError>>  Future         = Promise.GetFuture();
    const TByteArray                        RequestPayload = BuildPayload(Request);

    MPromise<TResult<TResponse, FAppError>> CompletionPromise = Promise;
    const uint64                            CallId            = RegisterServerCall(
        [Promise = std::move(CompletionPromise), FunctionNameValue = MString(FunctionName)](const SServerCallResponse& Response) mutable {
            if (Response.bSuccess) {
                TResponse ResponseValue{};
                auto      ParseResult = ParsePayload(Response.Payload, ResponseValue, FunctionNameValue.c_str());
                if (!ParseResult.IsOk()) {
                    Promise.SetValue(MakeRpcErrorResult<TResponse>("server_call_response_parse_failed", ParseResult.GetError().c_str()));
                    return;
                }

                Promise.SetValue(TResult<TResponse, FAppError>::Ok(std::move(ResponseValue)));
                return;
            }

            FAppError ErrorValue;
            auto      ParseResult = ParsePayload(Response.Payload, ErrorValue, FunctionNameValue.c_str());
            if (!ParseResult.IsOk()) {
                Promise.SetValue(MakeRpcErrorResult<TResponse>("server_call_error_parse_failed", ParseResult.GetError().c_str()));
                return;
            }

            Promise.SetValue(MakeErrorResult<TResponse>(std::move(ErrorValue)));
        },
        5.0, BuildServerCallLivenessProbe(Connection), FunctionId, GetServerCallConnectionIdentity(Connection));

    if (CallId == 0) {
        Promise.SetValue(MakeRpcErrorResult<TResponse>("server_call_register_failed", FunctionName));
        return Future;
    }

    TByteArray PacketPayload;
    if (!BuildServerCallPacket(FunctionId, CallId, RequestPayload, PacketPayload)) {
        CancelServerCall(CallId);
        Promise.SetValue(MakeRpcErrorResult<TResponse>("server_call_build_failed", FunctionName));
        return Future;
    }

    if (!SendServerCallMessage(std::forward<TConnection>(Connection), PacketPayload)) {
        CancelServerCall(CallId);
        Promise.SetValue(MakeRpcErrorResult<TResponse>("server_call_send_failed", FunctionName));
        return Future;
    }

    return Future;
}
