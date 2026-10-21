/**
 * @file Promise.h
 * @brief MFuture / MPromise —— 项目内的异步结果原语(规范 §7)。
 *
 * MFuture<T> 与 MPromise<T> 共享一份引用计数的状态
 * (MDetail::SPromiseState<T>):Promise 负责 SetValue/SetException,Future 负责
 * Wait/Await/Get/Then/Peek。T 与 void 各有一份状态结构(后者没有 Value 域)。
 *
 * 本头文件刻意不依赖协议头:只用前向声明引用 FAppError,使
 * MFuture<TResult<T, FAppError>> 可用而不必把 Protocol/Messages/Common/
 * AppMessages.h 拉进来。需要操作 FAppError(运算符、方法)的调用方自行 include。
 */
#pragma once

#include "Common/Runtime/Async/AsyncContext.h"
#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Object/Result.h"
#include <condition_variable>
#include <exception>

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
    // 非破坏性读取已就绪的结果(不会把值移动出去)。
    // 前置条件:IsReady() 为真;对未就绪的 future 调用会抛异常
    // (与 Wait/Await 的契约一致)。
    const T& Peek() const;

    /**
     * 注册完成回调。回调在兑现 Promise 的线程执行，仅用于结果桥接。
     *
     * 要恢复业务执行的代码必须使用 ThenOn() 并传入注册点捕获的上下文。
     * Promise 完成时不得从完成线程的 TLS 状态选择恢复上下文。
     */
    void Then(TFunction<void(MFuture<T>)> Callback);

    /** 注册一个在显式指定异步上下文中执行的回调。 */
    void ThenOn(TSharedPtr<MAsync::MAsyncContext> ResumeContext, TFunction<void(MFuture<T>)> Callback);

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
    void ThenOn(TSharedPtr<MAsync::MAsyncContext> ResumeContext, TFunction<void(MFuture<void>)> Callback);

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
        // 上下文由注册点选择，不由生产者选择。ThenOn 会以显式上下文包装
        // 回调；普通 Then 则刻意保持为就地执行的结果桥接原语。
        Callback(MFuture<T>(State));
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

// 非破坏性读取已就绪的结果:返回所存 T 的 const 引用,调用方可以查看值
// 而不必把它从共享状态里移走。前置条件:调用方必须已观察到
// IsReady() == true(或先调用过 Wait/Await);对未就绪的 future 调用会抛
// std::runtime_error。
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

template <typename T> void MFuture<T>::ThenOn(TSharedPtr<MAsync::MAsyncContext> ResumeContext, TFunction<void(MFuture<T>)> Callback) {
    if (!Callback) {
        return;
    }
    Then([ResumeContext, Callback = std::move(Callback)](MFuture<T> Completed) mutable {
        if (ResumeContext != nullptr) {
            ResumeContext->Post([Callback = std::move(Callback), Completed = std::move(Completed)]() mutable { Callback(std::move(Completed)); });
            return;
        }
        // 注册点没有安装 TLS 上下文。保持既有的独立 Future 行为，同时把
        // 该选择固定在注册点而不是完成点。
        Callback(std::move(Completed));
    });
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
        Callback(MFuture<void>(State));
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

inline void MFuture<void>::ThenOn(TSharedPtr<MAsync::MAsyncContext> ResumeContext, TFunction<void(MFuture<void>)> Callback) {
    if (!Callback) {
        return;
    }
    Then([ResumeContext, Callback = std::move(Callback)](MFuture<void> Completed) mutable {
        if (ResumeContext != nullptr) {
            ResumeContext->Post([Callback = std::move(Callback), Completed = std::move(Completed)]() mutable { Callback(std::move(Completed)); });
            return;
        }
        Callback(std::move(Completed));
    });
}
