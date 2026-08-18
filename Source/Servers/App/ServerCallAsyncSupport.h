#pragma once

#include "Common/Net/Rpc/RpcServerCall.h"
#include "Common/Runtime/Concurrency/Promise.h"
#include "Common/Runtime/Object/Result.h"
#include "Protocol/Messages/Common/AppMessages.h"

#include <type_traits>
#include <utility>

namespace MServerCallAsyncSupport
{
template<typename TResponse>
using TAppResult = TResult<TResponse, FAppError>;

template<typename TResponse>
using TAppFuture = MFuture<TAppResult<TResponse>>;

template<typename TResponse>
TAppFuture<TResponse> MakeResultFuture(TAppResult<TResponse> Result)
{
    MPromise<TAppResult<TResponse>> Promise;
    MFuture<TAppResult<TResponse>> Future = Promise.GetFuture();
    Promise.SetValue(std::move(Result));
    return Future;
}

template<typename TResponse>
TAppFuture<TResponse> MakeSuccessFuture(TResponse Response)
{
    return MakeResultFuture(TAppResult<TResponse>::Ok(std::move(Response)));
}

template<typename TResponse>
TAppFuture<TResponse> MakeErrorFuture(const char* Code, const char* Message = "")
{
    return MakeResultFuture(TAppResult<TResponse>::Err(FAppError::Make(Code, Message ? Message : "")));
}

template<typename TResponse>
TAppFuture<TResponse> MakeErrorFuture(const MString& Code, const MString& Message = MString())
{
    return MakeResultFuture(TAppResult<TResponse>::Err(FAppError::Make(Code, Message)));
}
/**
 * StartDeferredServerCall — 把 ServerCall handler 的返回值包装成 CallClient 异步响应。
 * 当前 PoC 仅支持同步 ServerCall（不直接生成 CallClient 包）。
 * TODO: 等反射驱动 CallClient dispatch 落地后，再实现异步回包路径。
 */
template<typename TResponse>
TAppFuture<TResponse> StartDeferredServerCall(
    const SServerCallContext& /*Context*/,
    TResponse Response,
    const char* /*FunctionName*/)
{
    return MakeSuccessFuture(std::move(Response));
}

template<typename TResponse>
TAppFuture<TResponse> StartDeferredServerCall(
    const SServerCallContext& /*Context*/,
    TAppResult<TResponse> Result,
    const char* /*FunctionName*/)
{
    return MakeResultFuture(std::move(Result));
}

} // namespace MServerCallAsyncSupport
