/**
 * @file MAsync.h
 * @brief MAsync —— async/await 类型层(SFutureResult / AWAIT_OK 的公共入口)。
 */
#pragma once

#include "Common/Runtime/Async/AsyncContext.h"
#include "Common/Runtime/Concurrency/Promise.h"
#include "Common/Runtime/Object/Result.h"
#include "Protocol/Messages/Common/AppMessages.h"

#include <cassert>

/**
 * @brief MAsync - async/await 类型层。
 *
 * @detail 核心类型:
 * - SFutureResult<T>:MFuture<TResult<T, FAppError>> 的别名;Get() 在 Err 时抛 FFutureResultError
 * - FFutureResultError:统一的 async 函数错误类型
 *
 * 用法（占位,C++17 异步模型完整说明见父 spec §5.1 / §7）:
 *
 * 业务侧声明异步函数时,在函数签名上挂 transport 标签（如 class 上的
 * ServerCall / CallClient / Async,namespace-scope 自由函数只允许纯
 * Async,见 spec 2026-07-28 §B）。函数体返回 SFutureResult<FResp> 或
 * 在帧内使用 AWAIT_OK(expr) 来挂起等待另一个 async 调用。完整 codegen
 * 由 MHeaderTool 完成,见 Docs/superpowers/specs/2026-07-24-cpp17-async-await.md
 * §7 与 §18 附录 A。
 */

// ============================================
// 统一错误类型
// ============================================

class FFutureResultError : public std::exception {
    public:
    explicit FFutureResultError(FAppError InError) : Error(std::move(InError)), Message(BuildMessage()) {
    }

    const char* what() const noexcept override {
        return Message.c_str();
    }

    const FAppError& GetError() const {
        return Error;
    }

    private:
    MString BuildMessage() const {
        if (Error.Code.empty()) {
            return Error.Message.empty() ? "async_operation_failed" : Error.Message;
        }
        return Error.Message.empty() ? Error.Code : Error.Code + ": " + Error.Message;
    }

    FAppError Error;
    MString   Message;
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
template <typename T> struct SFutureResult : MFuture<TResult<T, FAppError>> {
    using Super = MFuture<TResult<T, FAppError>>;
    using Super::Super;

    // InnerType — 协议层取内层类型的统一入口 (codegen / Awaitable.h 模板用)
    using InnerType = T;

    // 从基类隐式构造（兼容 MFuture<TResult<...>> 返回类型的既有代码）
    SFutureResult(const Super& Other) : Super(Other) {
    }
    SFutureResult(Super&& Other) : Super(std::move(Other)) {
    }

    // 隐式转换为基类（供旧的实现代码使用 MFuture<TResult<...>> 返回类型）
    operator Super() const {
        return *this;
    }

    // 从 TResult<T, FAppError> 构造（async 函数体内 early-return 路径）
    SFutureResult(const TResult<T, FAppError>& Result) {
        MPromise<TResult<T, FAppError>> Promise;
        Promise.SetValue(Result);
        *static_cast<Super*>(this) = Promise.GetFuture();
    }

    SFutureResult(TResult<T, FAppError>&& Result) {
        MPromise<TResult<T, FAppError>> Promise;
        Promise.SetValue(std::move(Result));
        *static_cast<Super*>(this) = Promise.GetFuture();
    }

    // T != void:存在 GetValue()
    template <typename U = T, std::enable_if_t<!std::is_same<U, void>::value, int> = 0> T Get() const {
        // Get 红线(规范 P1 §8.2):检测「loop 线程在等一个依赖本 loop 的
        // future」—— 那必然死锁。这里只记日志并 assert,不抛异常:
        // 把死锁换成另一种崩溃只会更难查。
        if (!this->IsReady()) {
            if (auto* Ctx = MAsync::MAsyncContext::Current()) {
                if (Ctx->IsSameContext()) {
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
        if (Result.IsErr()) {
            throw FFutureResultError(Result.GetError());
        }
        return Result.GetValue();
    }

    // T == void:TResult<void, E> 没有 GetValue()
    template <typename U = T, std::enable_if_t<std::is_same<U, void>::value, int> = 0> void Get() const {
        const TResult<void, FAppError>& Result = Super::Get();
        if (Result.IsErr()) {
            throw FFutureResultError(Result.GetError());
        }
    }

    // 返回原始 TResult，不抛
    template <typename U = T, std::enable_if_t<!std::is_same<U, void>::value, int> = 0> const TResult<T, FAppError>& PeekResult() const {
        // 非破坏性读取已就绪的结果。与 Get() 不同(Get 经 MFuture::Get() 把
        // 值移动出去),PeekResult 保留底层状态,调用方可以拿回同一个
        // SFutureResult 继续查询。仅当 IsReady() 为真时结果有效;未就绪的
        // 调用方应先通过 Get()/Wait() 阻塞。
        return Super::Peek();
    }
    TResult<T, FAppError> GetResult() const {
        return Super::Get();
    }

    bool IsOk() const {
        return GetResult().IsOk();
    }

    bool IsErr() const {
        return GetResult().IsErr();
    }

    const FAppError& GetError() const {
        return GetResult().GetError();
    }

    // ============================================
    // P5 类型层 — SAwaiter / AsAwaiter（KD-1, spec 2026-07-30-async-p5）
    // ============================================
    //
    // 对齐 C# `GetAwaiter()` 协议层：任何返回 SFutureResult<T> 的表达式
    // 都可被 await（codegen 经 TAwaitable 生成状态机）。三方法：
    //   AwaitReady()           — 是否已就绪（同步路径不挂起）
    //   AwaitSuspend(FrameT*)  — 未就绪时把续体（Frame->Resume()）挂到 Then
    //   AwaitResume()          — 就绪后取值；Err 抛 FFutureResultError
    // 协议层完全通用：新增 awaitable 类型只需实现这三方法（KD-1 §3.2）。
    struct SAwaiter {
        SFutureResult<T> Self;

        bool AwaitReady() const noexcept {
            return Self.IsReady();
        }

        template <typename FrameT> void AwaitSuspend(FrameT* Frame) {
            // Frame 由 P5 codegen 生成（业务侧不可见，KD-6）；此处只依赖
            // 其 Resume() 契约——future 完成时恢复状态机执行。
            Self.Then([Frame](MFuture<TResult<T, FAppError>> /*F*/) mutable { Frame->Resume(); });
        }

        T AwaitResume() {
            const TResult<T, FAppError>& Result = Self.GetResult();
            if (Result.IsErr()) {
                throw FFutureResultError(Result.GetError());
            }
            if constexpr (!std::is_void_v<T>) {
                return Result.GetValue();
            }
        }
    };

    SAwaiter AsAwaiter() {
        SAwaiter Awaiter;
        Awaiter.Self = *this; // awaiter 持 future 副本（KD-1 §3.2）
        return Awaiter;
    }
};

// ============================================
// _unwrap — 统一解包辅助（供 MHeaderTool 生成的状态机使用）
// ============================================

namespace MAsyncDetail {
    template <typename T> auto _unwrap(const MFuture<T>& Future) -> TResult<decltype(std::declval<T>().GetValue()), FAppError> {
        if (!Future.IsReady()) {
            return TResult<decltype(std::declval<T>().GetValue()), FAppError>::Err(FAppError{"future_not_ready", "Attempted to unwrap a future that is not yet ready"});
        }

        auto Result = Future.Get();
        if (Result.IsErr()) {
            return TResult<decltype(std::declval<T>().GetValue()), FAppError>::Err(Result.GetError());
        }
        return TResult<decltype(std::declval<T>().GetValue()), FAppError>::Ok(std::move(Result).GetValue());
    }

    template <typename T> auto _unwrap(const SFutureResult<T>& Future) -> TResult<T, FAppError> {
        return Future.GetResult();
    }

    // MFuture<void> 是具体类(不是模板实例),因此用普通重载
    inline TResult<void, FAppError> _unwrap(const MFuture<void>& Future) {
        if (!Future.IsReady()) {
            return TResult<void, FAppError>::Err(FAppError{"future_not_ready", ""});
        }
        Future.Get();
        return TResult<void, FAppError>::Ok();
    }

    // MFuture<TResult<void, E>>:TResult<void, E> 没有 GetValue(),需要显式重载
    template <typename E> [[nodiscard]] inline TResult<void, E> _unwrap(const MFuture<TResult<void, E>>& Future) {
        if (!Future.IsReady()) {
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
 * @brief TAsyncFuture<T> —— async 函数内部等待其他 async 函数时的返回类型,
 * 等价于 MFuture<TResult<T, FAppError>>。
 *
 * @detail 这是历史遗留的透传别名;新代码禁止使用,统一走 SFutureResult<T>。
 */
template <typename T> using TAsyncFuture = MFuture<TResult<T, FAppError>>;

// =========================================================================
// WrapAsSFutureResult — MFuture<T> -> SFutureResult<T> 桥接
// =========================================================================

/**
 * @brief 把裸 MFuture<T> 桥接成契约类型 SFutureResult<T>。
 *
 * @detail 生成的 ServerCall 适配层用它把 Inner.Then(serialize) 链上的
 * MFuture<TByteArray> 提升回 DispatchServerCall 期望的 SFutureResult<TByteArray>。
 *
 * 语义:
 *   - Inner 已就绪:同步构造一个已就绪的 SFutureResult
 *   - Inner 尚未就绪:注册 Then,等 Inner 完成时再给新 promise 设值,并返回
 *     尚未就绪的 SFutureResult
 *   - 异常路径:注意实现与早期设计文档不一致 —— 当前实现是「捕获异常后置
 *     默认值 T{}」,而不是把异常转成 TResult::Err("future_bridge_exception")。
 *     原因是裸 MFuture 的异常路径不带 FAppError 信息,无法还原出有意义的
 *     错误码。(若将来要暴露错误,应改为 Err 并同时补齐错误码来源。)
 *
 * SFutureResult<T> 本身已有从 MFuture<TResult<T, FAppError>> 转换的构造函数
 * (见本文件 SFutureResult 的 ctor);本helper 专门处理「没有 TResult 包装」
 * 的 MFuture<T> —— 即生成适配器里 Then(serialize) -> T 的那个场景。
 */
namespace MAsyncDetail {
    template <typename T> SFutureResult<T> WrapAsSFutureResult(MFuture<T> Inner) {
        MPromise<T> Promise;
        auto        Future = Promise.GetFuture();

        if (Inner.IsReady()) {
            try {
                Promise.SetValue(Inner.Get());
            } catch (...) {
                // 注意:这里实际是「吞掉异常并置默认值」。裸 MFuture 的异常
                // 路径不带 FAppError 信息,无法还原成有意义的错误码;当前选择
                // 降级为 T{}。若将来要暴露错误,需改为 Err 并补齐错误码来源。
                Promise.SetValue(T{});
            }
        } else {
            Inner.Then([Promise](MFuture<T> F) mutable {
                try {
                    Promise.SetValue(F.Get());
                } catch (...) {
                    Promise.SetValue(T{});
                }
            });
        }

        // SFutureResult<T> 有从 MFuture<TResult<T, FAppError>> 转换的构造函数,
        // 这里直接用它构造。
        return SFutureResult<T>(MFuture<TResult<T, FAppError>>(Future));
    }
} // namespace MAsyncDetail
