#pragma once

#include "Common/Runtime/Concurrency/Promise.h"
#include "Common/Runtime/Object/Result.h"
#include "Protocol/Messages/Common/AppMessages.h"

/**
 * MAsync - async/await 类型层
 *
 * 核心类型：
 * - SFutureResult<T>: MFuture<TResult<T, FAppError>> 的别名，Get() err 时抛 FFutureResultError
 * - FFutureResultError: 统一 async 函数错误类型
 *
 * 用法：
 *   #define MFUTURE(T) SFutureResult<T>
 *
 *   MFunction(ServerCall, Async)
 *   MFUTURE(FPlayerLogoutResponse) PlayerLogout(FPlayerLogoutRequest Request)
 *   {
 *       auto* Profile = AWAIT(ResolveProfile());
 *       AWAIT(SaveProfile(Profile));
 *       co_return FPlayerLogoutResponse{};
 *   }
 */

// ============================================
// 统一错误类型
// ============================================

class FFutureResultError : public std::exception
{
public:
    explicit FFutureResultError(FAppError InError)
        : Error(std::move(InError))
        , Message(BuildMessage())
    {
    }

    const char* what() const noexcept override
    {
        return Message.c_str();
    }

    const FAppError& GetError() const
    {
        return Error;
    }

private:
    MString BuildMessage() const
    {
        if (Error.Code.empty())
        {
            return Error.Message.empty() ? "async_operation_failed" : Error.Message;
        }
        return Error.Message.empty() ? Error.Code : Error.Code + ": " + Error.Message;
    }

    FAppError Error;
    MString Message;
};

// ============================================
// MFUTURE(T) — async 函数返回类型别名
// ============================================

/**
 * SFutureResult<T> — async 函数的标准返回类型
 *
 * 语义：
 * - 继承 MFuture<TResult<T, FAppError>>，完整保留 future 语义
 * - Get(): err 时抛 FFutureResultError（用于 fiber 内的 MAwait）
 * - GetResult(): err 时不抛，返回原始 TResult（用于需要判断错误的场景）
 * - IsOk() / IsErr() / GetError(): 便捷查询
 */
template<typename T>
struct SFutureResult : MFuture<TResult<T, FAppError>>
{
    using Super = MFuture<TResult<T, FAppError>>;
    using Super::Super;

    // 从基类隐式构造（供 FiberAwait 适配层使用）
    SFutureResult(const Super& Other) : Super(Other) {}
    SFutureResult(Super&& Other) : Super(std::move(Other)) {}

    // T != void：GetValue() exists
    template<typename U = T, std::enable_if_t<!std::is_same<U, void>::value, int> = 0>
    T Get() const
    {
        const TResult<T, FAppError>& Result = Super::Get();
        if (Result.IsErr())
        {
            throw FFutureResultError(Result.GetError());
        }
        return Result.GetValue();
    }

    // T == void：TResult<void, E> has no GetValue()
    template<typename U = T, std::enable_if_t<std::is_same<U, void>::value, int> = 0>
    void Get() const
    {
        const TResult<void, FAppError>& Result = Super::Get();
        if (Result.IsErr())
        {
            throw FFutureResultError(Result.GetError());
        }
    }

    // 返回原始 TResult，不抛
    TResult<T, FAppError> GetResult() const
    {
        return Super::Get();
    }

    bool IsOk() const
    {
        return GetResult().IsOk();
    }

    bool IsErr() const
    {
        return GetResult().IsErr();
    }

    const FAppError& GetError() const
    {
        return GetResult().GetError();
    }
};

/**
 * MFUTURE(T) — 简化 async 函数返回类型声明
 *
 * 用法：
 *   MFUTURE(FPlayerLogoutResponse)
 * 替代：
 *   SFutureResult<FPlayerLogoutResponse>
 * 替代：
 *   MFuture<TResult<FPlayerLogoutResponse, FAppError>>
 */
#define MFUTURE(T) SFutureResult<T>

// ============================================
// _unwrap — 统一解包辅助（供 MHeaderTool 生成的状态机使用）
// ============================================

namespace MAsyncDetail
{
template<typename T>
auto _unwrap(const MFuture<T>& Future) -> TResult<decltype(std::declval<T>().GetValue()), FAppError>
{
    if (!Future.IsReady())
    {
        return TResult<decltype(std::declval<T>().GetValue()), FAppError>::Err(FAppError{
            "future_not_ready",
            "Attempted to unwrap a future that is not yet ready"});
    }

    auto Result = Future.Get();
    if (Result.IsErr())
    {
        return TResult<decltype(std::declval<T>().GetValue()), FAppError>::Err(Result.GetError());
    }
    return TResult<decltype(std::declval<T>().GetValue()), FAppError>::Ok(std::move(Result).GetValue());
}

template<typename T>
auto _unwrap(const SFutureResult<T>& Future) -> TResult<T, FAppError>
{
    return Future.GetResult();
}

// MFuture<void> — concrete class (not a template instantiation), use regular overload
inline TResult<void, FAppError> _unwrap(const MFuture<void>& Future)
{
    if (!Future.IsReady())
    {
        return TResult<void, FAppError>::Err(FAppError{"future_not_ready", ""});
    }
    Future.Get();
    return TResult<void, FAppError>::Ok();
}

// MFuture<TResult<void, E>> — TResult<void, E> has no GetValue(), needs explicit overload
template<typename E>
[[nodiscard]]
inline TResult<void, E> _unwrap(const MFuture<TResult<void, E>>& Future)
{
    if (!Future.IsReady())
    {
        return TResult<void, E>::Err(E{"future_not_ready", ""});
    }
    return Future.Get();
}
} // namespace MAsyncDetail

// ============================================
// 便捷别名（与现有 TPlayerCommandFuture 兼容）
// ============================================

/**
 * TAsyncFuture<T> — async 函数内部等待其他 async 函数时的返回类型
 * 等价于 MFuture<TResult<T, FAppError>>
 */
template<typename T>
using TAsyncFuture = MFuture<TResult<T, FAppError>>;
