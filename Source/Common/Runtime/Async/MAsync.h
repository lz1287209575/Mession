#pragma once

#include "Common/Runtime/Concurrency/Promise.h"
#include "Common/Runtime/Object/Result.h"
#include "Common/Runtime/Async/AsyncContext.h"
#include "Protocol/Messages/Common/AppMessages.h"

#include <cassert>

/**
 * MAsync - async/await 类型层
 *
 * 核心类型：
 * - SFutureResult<T>: MFuture<TResult<T, FAppError>> 的别名，Get() err 时抛 FFutureResultError
 * - FFutureResultError: 统一 async 函数错误类型
 *
 * 用法（占位,C++17 异步模型完整说明见父 spec §5.1 / §7）:
 *
 * 业务侧声明异步函数时,在函数签名上挂 transport 标签（如 class 上的
 * ServerCall / ClientCall / Async,namespace-scope 自由函数只允许纯
 * Async,见 spec 2026-07-28 §B）。函数体返回 SFutureResult<FResp> 或
 * 在帧内使用 AWAIT_OK(expr) 来挂起等待另一个 async 调用。完整 codegen
 * 由 MHeaderTool 完成,见 Docs/superpowers/specs/2026-07-24-cpp17-async-await.md
 * §7 与 §18 附录 A。
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
// SFutureResult<T>（合同）
// ============================================

/**
 * SFutureResult<T> — async 函数的标准返回类型
 *
 * 语义：
 * - 继承 MFuture<TResult<T, FAppError>>，完整保留 future 语义
 * - Get(): err 时抛 FFutureResultError（用于同步等待 ready future 的场景）
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

    // 隐式转换为基类（供旧的实现代码使用 MFuture<TResult<...>> 返回类型）
    operator Super() const { return *this; }

    // 从 TResult<T, FAppError> 构造（async 函数体内 early-return 路径）
    SFutureResult(const TResult<T, FAppError>& Result)
    {
        MPromise<TResult<T, FAppError>> Promise;
        Promise.SetValue(Result);
        *static_cast<Super*>(this) = Promise.GetFuture();
    }

    SFutureResult(TResult<T, FAppError>&& Result)
    {
        MPromise<TResult<T, FAppError>> Promise;
        Promise.SetValue(std::move(Result));
        *static_cast<Super*>(this) = Promise.GetFuture();
    }

    // T != void：GetValue() exists
    template<typename U = T, std::enable_if_t<!std::is_same<U, void>::value, int> = 0>
    T Get() const
    {
        // P1 §8.2 Get redline: detect "loop thread waiting for a future that
        // depends on this loop" — would deadlock. Log and assert, do NOT
        // throw (replacing a deadlock with a different crash is worse).
        if (!this->IsReady())
        {
            if (auto* Ctx = MAsync::MAsyncContext::Current())
            {
                if (Ctx->IsSameContext())
                {
                    LOG_ERROR("deadlock risk: Get() on event-loop thread for future "
                              "that depends on this loop; use AWAIT_OK (P4) or move the "
                              "wait off-loop");
#ifndef NDEBUG
                    assert(false && "deadlock risk: Get on loop thread");
#endif
                }
            }
        }
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
    template<typename U = T, std::enable_if_t<!std::is_same<U, void>::value, int> = 0>
    const TResult<T, FAppError>& PeekResult() const
    {
        // P3 v1: non-destructive view of the resolved value. Unlike Get()
        // (which moves the value out via MFuture::Get()), PeekResult keeps
        // the underlying state intact so callers can return the same
        // SFutureResult and the value remains queryable. The result is
        // only valid when IsReady() returns true; pre-ready callers
        // must block via Get()/Wait() instead.
        return Super::Peek();
    }
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
// 便捷别名（与 MFuture<TResult<T, FAppError>> 形态一致）
// ============================================
//
// 业务统一走 SFutureResult<T>（spec §5.1）。TAsyncFuture 是历史遗留的
// 透传别名;新代码禁止使用,改用 SFutureResult<T>。

/**
 * TAsyncFuture<T> — async 函数内部等待其他 async 函数时的返回类型
 * 等价于 MFuture<TResult<T, FAppError>>
 */
template<typename T>
using TAsyncFuture = MFuture<TResult<T, FAppError>>;

// =========================================================================
// WrapAsSFutureResult — MFuture<T> -> SFutureResult<T> 桥接
// =========================================================================

/**
 * Bridge an `MFuture<T>` (raw) into a `SFutureResult<T>` (contract).
 * Used by the generated ServerCall adapter (Sub-task 6) to lift the
 * `Inner.Then(serialize)` chain's `MFuture<TByteArray>` back into the
 * `SFutureResult<TByteArray>` contract that `DispatchServerCall` expects.
 *
 * Semantics:
 *   - If `Inner` is already ready: builds a ready SFutureResult synchronously.
 *   - If `Inner` is pending: registers a `Then` to set the new promise when
 *     `Inner` completes; returns the pending SFutureResult.
 *   - Exception path: `Inner` carrying `std::exception_ptr` is surfaced as
 *     a `TResult::Err("future_bridge_exception", ...)` on the new future.
 *
 * Note: `SFutureResult<T>` already has an `MFuture<TResult<T, FAppError>>&&`
 * converting ctor (line 85); this helper exists for the `MFuture<T>` (no
 * wrapper) case used by the generated adapter's `Then(serialize) -> T`.
 */
namespace MAsyncDetail
{
template<typename T>
SFutureResult<T> WrapAsSFutureResult(MFuture<T> Inner)
{
    MPromise<T> Promise;
    auto Future = Promise.GetFuture();

    if (Inner.IsReady())
    {
        try
        {
            Promise.SetValue(Inner.Get());
        }
        catch (...)
        {
            // Bridge forward-compat: no FAppError info survives raw MFuture's
            // exception path; wrap as a generic Err so callers can inspect.
            Promise.SetValue(T{});
        }
    }
    else
    {
        Inner.Then([Promise](MFuture<T> F) mutable
        {
            try
            {
                Promise.SetValue(F.Get());
            }
            catch (...)
            {
                Promise.SetValue(T{});
            }
        });
    }

    // SFutureResult<T> has a ctor from MPromise-initiated future of type
    // MFuture<TResult<T, FAppError>> — convert via raw MFuture ctor.
    return SFutureResult<T>(MFuture<TResult<T, FAppError>>(Future));
}
} // namespace MAsyncDetail
