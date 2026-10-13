/**
 * @file ServerCallAsyncSupport.h
 * @brief MServerCallAsyncSupport —— ServerCall 返回值构造 / 异步回包辅助。
 *
 * @detail 约定:每个 MFUNCTION(ServerCall) 的 handler 统一返回
 *   SFutureResult<TResp>(= MFuture<TResult<TResp, FAppError>>)。业务实现用这里的
 *   MakeSuccessFuture / MakeErrorFuture / MakeResultFuture 把"已经算好的结果"包成
 *   ready future —— 三者都立即完成、Settled 状态下不占用事件循环。
 *   StartDeferredServerCall 是为"handler 挂起、稍后再回包"预留的入口;当前 PoC 阶段
 *   仍走同步路径,其 Context / FunctionName 形参被忽略(保留形参是为了冻结调用方签名)。
 */
#pragma once

#include "Common/Net/Rpc/RpcServerCall.h"
#include "Common/Runtime/Concurrency/Promise.h"
#include "Common/Runtime/Object/Result.h"
#include "Protocol/Messages/Common/AppMessages.h"

#include <type_traits>
#include <utility>

namespace MServerCallAsyncSupport {
    /** @brief TAppResult - ServerCall 响应的统一结果类型(值或 FAppError)。 */
    template <typename TResponse> using TAppResult = TResult<TResponse, FAppError>;

    /** @brief TAppFuture - TAppResult 对应的 future 别名(ServerCall handler 的返回类型)。 */
    template <typename TResponse> using TAppFuture = MFuture<TAppResult<TResponse>>;

    /**
     * @brief MakeResultFuture - 把已完成的结果包成一个 ready future。
     * @param Result 已算出的结果(Ok 或 Err)
     * @return 立刻处于 ready 状态的 future
     */
    template <typename TResponse> TAppFuture<TResponse> MakeResultFuture(TAppResult<TResponse> Result) {
        MPromise<TAppResult<TResponse>> Promise;
        MFuture<TAppResult<TResponse>>  Future = Promise.GetFuture();
        Promise.SetValue(std::move(Result));
        return Future;
    }

    /** @brief MakeSuccessFuture - 包一个 Ok 结果(直接给值,不需要手工构造 TResult)。 */
    template <typename TResponse> TAppFuture<TResponse> MakeSuccessFuture(TResponse Response) {
        return MakeResultFuture(TAppResult<TResponse>::Ok(std::move(Response)));
    }

    /**
     * @brief MakeErrorFuture - 包一个 Err 结果。
     * @param Code    错误码(必填,如 "actor_id_required")
     * @param Message 人类可读描述(可省)
     *
     * @detail const char* 重载对 Message 做了 nullptr 兜底(nullptr 视为空串)。
     */
    template <typename TResponse> TAppFuture<TResponse> MakeErrorFuture(const char* Code, const char* Message = "") {
        return MakeResultFuture(TAppResult<TResponse>::Err(FAppError::Make(Code, Message ? Message : "")));
    }

    /** @brief MakeErrorFuture - 包一个 Err 结果(MString 重载,不做 nullptr 兜底)。 */
    template <typename TResponse> TAppFuture<TResponse> MakeErrorFuture(const MString& Code, const MString& Message = MString()) {
        return MakeResultFuture(TAppResult<TResponse>::Err(FAppError::Make(Code, Message)));
    }
    /**
     * @brief StartDeferredServerCall - 把 ServerCall handler 的返回值包装成可回给调用方的 future。
     * @param Context      ServerCall 上下文(当前实现忽略)
     * @param Response     已算出的响应体(Ok 形态重载)
     * @param FunctionName 函数名,仅用于诊断(当前实现忽略)
     * @return 立刻 ready 的 future
     *
     * @detail 当前 PoC 只支持同步 ServerCall:直接返回 MakeSuccessFuture(Response),
     *   不生成任何 CallClient 包。等反射驱动的 CallClient dispatch 落地后再补异步路径。
     * TODO: 等反射驱动 CallClient dispatch 落地后，再实现异步回包路径。
     */
    template <typename TResponse> TAppFuture<TResponse> StartDeferredServerCall(const SServerCallContext& /*Context*/, TResponse Response, const char* /*FunctionName*/) {
        return MakeSuccessFuture(std::move(Response));
    }

    /** @brief StartDeferredServerCall - 已完成结果(Ok/Err)版本的重载,直接透传结果。 */
    template <typename TResponse> TAppFuture<TResponse> StartDeferredServerCall(const SServerCallContext& /*Context*/, TAppResult<TResponse> Result, const char* /*FunctionName*/) {
        return MakeResultFuture(std::move(Result));
    }

} // namespace MServerCallAsyncSupport
