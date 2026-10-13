/**
 * @file RpcErrors.h
 * @brief RPC 失败返回的统一构造助手 —— 把 (错误码, 消息) 打包成 TResult / MFuture。
 *
 * @detail 只依赖 Promise / Result / AppMessages,不依赖任何业务类型,因此 Common 层可随意
 *   include。MRpcChannel / MActorRouter / RpcServerCall 等所有 RPC 失败路径都汇流到这里,
 *   使上层能够稳定地按错误码字符串分支,而不必区分失败来自哪个调用点。
 */

#pragma once

#include "Common/Runtime/Concurrency/Promise.h"
#include "Common/Runtime/Object/Result.h"
#include "Protocol/Messages/Common/AppMessages.h"

/**
 * @brief MakeRpcErrorResult - 构造一个恒为 Err 状态的 TResult。
 * @param Code    错误码;传 nullptr 时回退为 "rpc_error"
 * @param Message 人类可读的错误详情;传 nullptr 时按空串处理
 * @return Err 状态的 TResult<TResponse, FAppError>(TResponse 无需可默认构造)
 *
 * @detail 两个参数都做空指针兜底,调用方可以直接把可能为空的 ClassName / FunctionName 传进来。
 */
template <typename TResponse> inline TResult<TResponse, FAppError> MakeRpcErrorResult(const char* Code, const char* Message = "") {
    return MakeErrorResult<TResponse>(FAppError::Make(Code ? Code : "rpc_error", Message ? Message : ""));
}

/**
 * @brief MakeRpcErrorFuture - 构造一个已 ready 的错误 MFuture。
 * @param Code    错误码,语义同 MakeRpcErrorResult
 * @param Message 错误详情,语义同 MakeRpcErrorResult
 * @return 已 SetValue 为 Err 的 MFuture
 *
 * @detail 内部用局部 MPromise 立刻兑现,所以返回的 future 必定是 ready 的。专供
 *   "连接不可用 / 类名找不到" 这类同步失败点使用,调用方无需再写 pending 分支。
 */
template <typename TResponse> inline MFuture<TResult<TResponse, FAppError>> MakeRpcErrorFuture(const char* Code, const char* Message = "") {
    MPromise<TResult<TResponse, FAppError>> Promise;
    MFuture<TResult<TResponse, FAppError>>  Future = Promise.GetFuture();
    Promise.SetValue(MakeRpcErrorResult<TResponse>(Code, Message));
    return Future;
}
