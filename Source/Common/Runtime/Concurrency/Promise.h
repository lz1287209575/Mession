#pragma once

#include "Common/Runtime/Async/AsyncContext.h"
#include "Common/Runtime/Log/Log.h"
#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Object/Result.h"
#include <condition_variable>
#include <exception>

// FAppError lives in Protocol/Messages/Common/AppMessages.h, but Promise.h
// is a low-level building block that shouldn't depend on protocol types.
// Forward-declare it so MFuture<TResult<T, FAppError>> can be referenced
// without pulling the protocol header. Callers that need full FAppError
// access (operators, methods) include AppMessages.h themselves.
struct FAppError;

template <typename T> class MFuture;

namespace MDetail {
    template <typename T> struct SPromiseState {
        std::mutex                           Mutex;
        std::condition_variable              Cond;
        TOptional<T>                         Value;
        std::exception_ptr                   Exception;
        bool                                 Ready           = false;
        bool                                 FutureRetrieved = false;
        TVector<TFunction<void(MFuture<T>)>> ThenCallbacks;
    };

    struct SPromiseStateVoid {
        std::mutex                              Mutex;
        std::condition_variable                 Cond;
        std::exception_ptr                      Exception;
        bool                                    Ready           = false;
        bool                                    FutureRetrieved = false;
        TVector<TFunction<void(MFuture<void>)>> ThenCallbacks;
    };
} // namespace MDetail

template <typename T> class MFuture;

template <typename T> class MPromise {
    public:
    /** Completion source：生产异步结果，和 MFuture 成对出现。 */
    MPromise() : State(MakeShared<MDetail::SPromiseState<T>>()) {
    }

    MFuture<T> GetFuture();

    void SetValue(const T& Val);
    void SetValue(T&& Val);
    void SetException(std::exception_ptr E);

    private:
    TSharedPtr<MDetail::SPromiseState<T>> State;

    /** @brief P5: 同步 vs ambient.Post 分发(回原上下文)。 */
    void DispatchCallbacks(TVector<TFunction<void(MFuture<T>)>>& Callbacks);

    friend class MFuture<T>;
};

template <typename T> class MFuture {
    public:
    /** Result handle：消费异步结果，可 Wait/Await/Then。 */
    MFuture() = default;
    explicit MFuture(TSharedPtr<MDetail::SPromiseState<T>> InState) : State(std::move(InState)) {
    }

    bool Valid() const {
        return State != nullptr;
    }
    bool IsReady() const;

    void Wait() const;
    T    Await() const;
    T    Get() const;
    // P3 v1: non-destructive view of resolved value (does NOT move out).
    // Pre-condition: IsReady() must be true; calling on a non-ready
    // future throws (same contract as Wait/Await).
    const T& Peek() const;

    void Then(TFunction<void(MFuture<T>)> Callback);

    private:
    TSharedPtr<MDetail::SPromiseState<T>> State;
    friend class MPromise<T>;
};

// void 特化：无返回值
template <> class MPromise<void> {
    public:
    /** Completion source：生产异步结果，和 MFuture<void> 成对出现。 */
    MPromise() : State(MakeShared<MDetail::SPromiseStateVoid>()) {
    }

    MFuture<void> GetFuture();

    void SetValue();
    void SetException(std::exception_ptr E);

    private:
    TSharedPtr<MDetail::SPromiseStateVoid> State;

    /** @brief P5: void 特化版的 DispatchCallbacks。 */
    void DispatchCallbacks(TVector<TFunction<void(MFuture<void>)>>& Callbacks);

    friend class MFuture<void>;
};

template <> class MFuture<void> {
    public:
    /** Result handle：消费异步结果，可 Wait/Await/Then。 */
    MFuture() = default;
    explicit MFuture(TSharedPtr<MDetail::SPromiseStateVoid> InState) : State(std::move(InState)) {
    }

    bool Valid() const {
        return State != nullptr;
    }
    bool IsReady() const;

    void Wait() const;
    void Await() const;
    void Get() const;

    void Then(TFunction<void(MFuture<void>)> Callback);

    private:
    TSharedPtr<MDetail::SPromiseStateVoid> State;
    friend class MPromise<void>;
};

// ========== 实现 ==========

template <typename T> MFuture<T> MPromise<T>::GetFuture() {
    std::lock_guard<std::mutex> Lock(State->Mutex);
    if (State->FutureRetrieved) {
        return MFuture<T>();
    }
    State->FutureRetrieved = true;
    return MFuture<T>(State);
}

template <typename T> void MPromise<T>::SetValue(const T& Val) {
    TVector<TFunction<void(MFuture<T>)>> Callbacks;
    {
        std::lock_guard<std::mutex> Lock(State->Mutex);
        if (State->Ready) {
            return;
        }
        State->Value = Val;
        State->Ready = true;
        Callbacks    = std::move(State->ThenCallbacks);
    }
    State->Cond.notify_all();
    DispatchCallbacks(Callbacks);
}

template <typename T> void MPromise<T>::DispatchCallbacks(TVector<TFunction<void(MFuture<T>)>>& Callbacks) {
    for (auto& Callback : Callbacks) {
        if (!Callback) {
            continue;
        }
        // P5: 优先投递到 ambient(回原 Sub / 原线程),保持续体执行在原上下文
        // 无 ambient(Worker 线程 / 测试)走同步路径,行为不变
        if (auto* Ctx = MAsync::MAsyncContext::Current()) {
            if (Ctx->IsSameContext()) {
                // SetValue 在 ambient 自己的执行线程上,Post 进去会下一帧才跑
                // 业务在当前线程等这个 future ready → 死锁风险。redline 提示。
                LOG_ERROR("deadlock risk: SetValue on loop thread for future awaited in same loop; "
                          "use AWAIT_OK (P4) or move the wait off-loop");
#ifndef NDEBUG
                assert(false && "deadlock risk: SetValue on loop thread");
#endif
            }
            // 仍然 Post,让续体在 ambient 的执行线程跑(回原 Sub)
            Ctx->Post([Callback = std::move(Callback), State = State]() mutable { Callback(MFuture<T>(State)); });
        } else {
            // 无 ambient:同步跑(Worker 线程 / 测试 / 早期代码)
            Callback(MFuture<T>(State));
        }
    }
}

template <typename T> void MPromise<T>::SetValue(T&& Val) {
    TVector<TFunction<void(MFuture<T>)>> Callbacks;
    {
        std::lock_guard<std::mutex> Lock(State->Mutex);
        if (State->Ready) {
            return;
        }
        State->Value = std::move(Val);
        State->Ready = true;
        Callbacks    = std::move(State->ThenCallbacks);
    }
    State->Cond.notify_all();
    DispatchCallbacks(Callbacks);
}

template <typename T> void MPromise<T>::SetException(std::exception_ptr E) {
    TVector<TFunction<void(MFuture<T>)>> Callbacks;
    {
        std::lock_guard<std::mutex> Lock(State->Mutex);
        if (State->Ready) {
            return;
        }
        State->Exception = std::move(E);
        State->Ready     = true;
        Callbacks        = std::move(State->ThenCallbacks);
    }
    State->Cond.notify_all();
    for (auto& Callback : Callbacks) {
        if (Callback) {
            Callback(MFuture<T>(State));
        }
    }
}

template <typename T> bool MFuture<T>::IsReady() const {
    if (!State) {
        return false;
    }

    std::lock_guard<std::mutex> Lock(State->Mutex);
    return State->Ready;
}

template <typename T> void MFuture<T>::Wait() const {
    if (!State) {
        throw std::runtime_error("Await on invalid MFuture");
    }
    std::unique_lock<std::mutex> Lock(State->Mutex);
    State->Cond.wait(Lock, [this]() { return State->Ready; });
}

template <typename T> T MFuture<T>::Await() const {
    Wait();
    if (State->Exception) {
        std::rethrow_exception(State->Exception);
    }
    return std::move(*State->Value);
}

template <typename T> T MFuture<T>::Get() const {
    return Await();
}

// P3 v1: non-destructive read of the resolved value. Returns a const
// reference to the stored T so callers can inspect the value without
// moving it out of the shared state. Pre-condition: caller must have
// observed IsReady() == true (or called Wait/Await first); calling on
// a non-ready future throws std::runtime_error.
template <typename T> const T& MFuture<T>::Peek() const {
    if (!State) {
        throw std::runtime_error("Peek on invalid MFuture");
    }
    std::unique_lock<std::mutex> Lock(State->Mutex);
    if (!State->Ready) {
        throw std::runtime_error("Peek on non-ready MFuture");
    }
    return *State->Value;
}

template <typename T> void MFuture<T>::Then(TFunction<void(MFuture<T>)> Callback) {
    if (!State || !Callback) {
        return;
    }
    bool bCallNow = false;
    {
        std::lock_guard<std::mutex> Lock(State->Mutex);
        if (State->Ready) {
            bCallNow = true;
        } else {
            State->ThenCallbacks.push_back(Callback);
        }
    }
    if (bCallNow) {
        Callback(MFuture<T>(State));
    }
}

// void 特化实现
inline MFuture<void> MPromise<void>::GetFuture() {
    std::lock_guard<std::mutex> Lock(State->Mutex);
    if (State->FutureRetrieved) {
        return MFuture<void>();
    }
    State->FutureRetrieved = true;
    return MFuture<void>(State);
}

inline void MPromise<void>::SetValue() {
    TVector<TFunction<void(MFuture<void>)>> Callbacks;
    {
        std::lock_guard<std::mutex> Lock(State->Mutex);
        if (State->Ready) {
            return;
        }
        State->Ready = true;
        Callbacks    = std::move(State->ThenCallbacks);
    }
    State->Cond.notify_all();
    DispatchCallbacks(Callbacks);
}

inline void MPromise<void>::DispatchCallbacks(TVector<TFunction<void(MFuture<void>)>>& Callbacks) {
    for (auto& Callback : Callbacks) {
        if (!Callback) {
            continue;
        }
        if (auto* Ctx = MAsync::MAsyncContext::Current()) {
            if (Ctx->IsSameContext()) {
                LOG_ERROR("deadlock risk: SetValue on loop thread for future awaited in same loop; "
                          "use AWAIT_OK (P4) or move the wait off-loop");
#ifndef NDEBUG
                assert(false && "deadlock risk: SetValue on loop thread");
#endif
            }
            Ctx->Post([Callback = std::move(Callback), State = State]() mutable { Callback(MFuture<void>(State)); });
        } else {
            Callback(MFuture<void>(State));
        }
    }
}

inline void MPromise<void>::SetException(std::exception_ptr E) {
    TVector<TFunction<void(MFuture<void>)>> Callbacks;
    {
        std::lock_guard<std::mutex> Lock(State->Mutex);
        if (State->Ready) {
            return;
        }
        State->Exception = std::move(E);
        State->Ready     = true;
        Callbacks        = std::move(State->ThenCallbacks);
    }
    State->Cond.notify_all();
    for (auto& Callback : Callbacks) {
        if (Callback) {
            Callback(MFuture<void>(State));
        }
    }
}

inline bool MFuture<void>::IsReady() const {
    if (!State) {
        return false;
    }

    std::lock_guard<std::mutex> Lock(State->Mutex);
    return State->Ready;
}

inline void MFuture<void>::Wait() const {
    if (!State) {
        throw std::runtime_error("Await on invalid MFuture");
    }
    std::unique_lock<std::mutex> Lock(State->Mutex);
    State->Cond.wait(Lock, [this]() { return State->Ready; });
}

inline void MFuture<void>::Await() const {
    Wait();
    if (State->Exception) {
        std::rethrow_exception(State->Exception);
    }
}

inline void MFuture<void>::Get() const {
    Await();
}

inline void MFuture<void>::Then(TFunction<void(MFuture<void>)> Callback) {
    if (!State || !Callback) {
        return;
    }
    bool bCallNow = false;
    {
        std::lock_guard<std::mutex> Lock(State->Mutex);
        if (State->Ready) {
            bCallNow = true;
        } else {
            State->ThenCallbacks.push_back(Callback);
        }
    }
    if (bCallNow) {
        Callback(MFuture<void>(State));
    }
}
