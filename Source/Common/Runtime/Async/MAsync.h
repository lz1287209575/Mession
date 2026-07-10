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

    // 隐式转换为基类（供旧的实现代码使用 MFuture<TResult<...>> 返回类型）
    operator Super() const { return *this; }

    // 从 TResult<T, FAppError> 构造（供 co_return 使用）
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
 *
 * ----------------------------------------------------------------------------
 * 何时用 SFutureResult / MFUTURE vs 裸 MFuture：
 *
 *   - `MFUTURE(T)`（即 `SFutureResult<T>`）是**协程友好的 future**：
 *     Get() 返回值；Err 时抛 `FFutureResultError` 而不是返回错误码——适合
 *     `MAwait`/`MAwaitOk` 内部捕获并短路传播。
 *     所有 `MFUNCTION(ServerCall)` / `MFUNCTION(Async)` 的 RPC handler 返回
 *     类型都用 MFUTURE(T)。
 *
 *   - `MFuture<TResult<T, FAppError>>`（裸）适合**调用方**：
 *     当你需要明确检查 IsErr()/IsOk() 而不希望异常时，回包直接用 `.Then()`
 *     处理；不要在 MFUTURE 上调 .Get() 除非你愿意 catch FFutureResultError。
 *
 *   - `MFuture<void>` / `MPromise<void>` 是不带错误状态的纯 future。
 *
 * ----------------------------------------------------------------------------
 * Fiber 后端状态：
 *
 *   - Linux（POSIX ucontext）：完整 suspend/resume，`MAwait*` 走慢路径真挂起；
 *   - Windows（null backend）：fiber 落空，`MAwait*` 走到 suspend 会抛
 *     `FPlayerCommandError(FAppError::Make("fiber_backend_unsupported"))`。
 *
 *   **同步 fast-path 仍然安全**：`MAwait` / `MAwaitOk` 先检查
 *   `MHasCurrentPlayerCommand()`（即 fiber 是否在线程内），如果不在 fiber
 *   就直接调 `Future.Get()`，不进入 suspend/resume。任何在 MainLoop 上跑的
 *   service handler 都走 fast-path，不会触发 fiber backend 检查。
 *
 * ----------------------------------------------------------------------------
 * 服务 handler 的写法约定：
 *
 *   MFUNCTION(ServerCall)
 *   MFUTURE(FPlayerLogoutResponse) PlayerLogout(FPlayerLogoutRequest Request)
 *   {
 *       FPlayerLogoutResponse Response;
 *       // ... 业务逻辑 ...
 *       return MServerCallAsyncSupport::MakeSuccessFuture(std::move(Response));
 *   }
 *
 *   错误用 MServerCallAsyncSupport::MakeErrorFuture<T>(code, msg)，
 *   不要直接构造 `TResult<T, FAppError>::Err(...)`——它跟服务层的 wire
 *   code 协议绑定（生成在 Build/Generated/MClientManifest.mgenerated.cpp）。
 *
 *   不要在 service handler 里调 `MAwait` / `MAwaitOk` ——它们目前是为 PoC
 *   异步链路预留的 hook。如果你的服务逻辑需要"等待另一个 future 完成"，
 *   用 `.Then()` 链 + 接续函数；只有当业务真正实现 player strand 调度时
 *   才用 fiber await。
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
