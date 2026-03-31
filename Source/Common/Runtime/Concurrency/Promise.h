#pragma once

#include "Common/Runtime/MLib.h"
#include <exception>
#include <condition_variable>

template<typename T>
class MFuture;

namespace MDetail
{
template<typename T>
struct SPromiseState
{
    std::mutex Mutex;
    std::condition_variable Cond;
    TOptional<T> Value;
    std::exception_ptr Exception;
    bool Ready = false;
    bool FutureRetrieved = false;
    TVector<TFunction<void(MFuture<T>)>> ThenCallbacks;
};

struct SPromiseStateVoid
{
    std::mutex Mutex;
    std::condition_variable Cond;
    std::exception_ptr Exception;
    bool Ready = false;
    bool FutureRetrieved = false;
    TVector<TFunction<void(MFuture<void>)>> ThenCallbacks;
};
}

template<typename T>
class MFuture;

template<typename T>
class MPromise
{
public:
    /** Completion source：生产异步结果，和 MFuture 成对出现。 */
    MPromise() : State(MakeShared<MDetail::SPromiseState<T>>()) {}

    MFuture<T> GetFuture();

    void SetValue(const T& Val);
    void SetValue(T&& Val);
    void SetException(std::exception_ptr E);

private:
    TSharedPtr<MDetail::SPromiseState<T>> State;
    friend class MFuture<T>;
};

template<typename T>
class MFuture
{
public:
    /** Result handle：消费异步结果，可 Wait/Await/Then。 */
    MFuture() = default;
    explicit MFuture(TSharedPtr<MDetail::SPromiseState<T>> InState) : State(std::move(InState)) {}

    bool Valid() const { return State != nullptr; }
    bool IsReady() const;

    void Wait() const;
    T Await() const;
    T Get() const;

    void Then(TFunction<void(MFuture<T>)> Callback);

private:
    TSharedPtr<MDetail::SPromiseState<T>> State;
    friend class MPromise<T>;
};

// void 特化：无返回值
template<>
class MPromise<void>
{
public:
    /** Completion source：生产异步结果，和 MFuture<void> 成对出现。 */
    MPromise() : State(MakeShared<MDetail::SPromiseStateVoid>()) {}

    MFuture<void> GetFuture();

    void SetValue();
    void SetException(std::exception_ptr E);

private:
    TSharedPtr<MDetail::SPromiseStateVoid> State;
    friend class MFuture<void>;
};

template<>
class MFuture<void>
{
public:
    /** Result handle：消费异步结果，可 Wait/Await/Then。 */
    MFuture() = default;
    explicit MFuture(TSharedPtr<MDetail::SPromiseStateVoid> InState) : State(std::move(InState)) {}

    bool Valid() const { return State != nullptr; }
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

template<typename T>
MFuture<T> MPromise<T>::GetFuture()
{
    std::lock_guard<std::mutex> Lock(State->Mutex);
    if (State->FutureRetrieved)
    {
        return MFuture<T>();
    }
    State->FutureRetrieved = true;
    return MFuture<T>(State);
}

template<typename T>
void MPromise<T>::SetValue(const T& Val)
{
    TVector<TFunction<void(MFuture<T>)>> Callbacks;
    {
        std::lock_guard<std::mutex> Lock(State->Mutex);
        if (State->Ready)
        {
            return;
        }
        State->Value = Val;
        State->Ready = true;
        Callbacks = std::move(State->ThenCallbacks);
    }
    State->Cond.notify_all();
    for (auto& Callback : Callbacks)
    {
        if (Callback)
        {
            Callback(MFuture<T>(State));
        }
    }
}

template<typename T>
void MPromise<T>::SetValue(T&& Val)
{
    TVector<TFunction<void(MFuture<T>)>> Callbacks;
    {
        std::lock_guard<std::mutex> Lock(State->Mutex);
        if (State->Ready)
        {
            return;
        }
        State->Value = std::move(Val);
        State->Ready = true;
        Callbacks = std::move(State->ThenCallbacks);
    }
    State->Cond.notify_all();
    for (auto& Callback : Callbacks)
    {
        if (Callback)
        {
            Callback(MFuture<T>(State));
        }
    }
}

template<typename T>
void MPromise<T>::SetException(std::exception_ptr E)
{
    TVector<TFunction<void(MFuture<T>)>> Callbacks;
    {
        std::lock_guard<std::mutex> Lock(State->Mutex);
        if (State->Ready)
        {
            return;
        }
        State->Exception = std::move(E);
        State->Ready = true;
        Callbacks = std::move(State->ThenCallbacks);
    }
    State->Cond.notify_all();
    for (auto& Callback : Callbacks)
    {
        if (Callback)
        {
            Callback(MFuture<T>(State));
        }
    }
}

template<typename T>
bool MFuture<T>::IsReady() const
{
    if (!State)
    {
        return false;
    }

    std::lock_guard<std::mutex> Lock(State->Mutex);
    return State->Ready;
}

template<typename T>
void MFuture<T>::Wait() const
{
    if (!State)
    {
        throw std::runtime_error("Await on invalid MFuture");
    }
    std::unique_lock<std::mutex> Lock(State->Mutex);
    State->Cond.wait(Lock, [this]() { return State->Ready; });
}

template<typename T>
T MFuture<T>::Await() const
{
    Wait();
    if (State->Exception)
    {
        std::rethrow_exception(State->Exception);
    }
    return std::move(*State->Value);
}

template<typename T>
T MFuture<T>::Get() const
{
    return Await();
}

template<typename T>
void MFuture<T>::Then(TFunction<void(MFuture<T>)> Callback)
{
    if (!State || !Callback)
    {
        return;
    }
    bool bCallNow = false;
    {
        std::lock_guard<std::mutex> Lock(State->Mutex);
        if (State->Ready)
        {
            bCallNow = true;
        }
        else
        {
            State->ThenCallbacks.push_back(Callback);
        }
    }
    if (bCallNow)
    {
        Callback(MFuture<T>(State));
    }
}

// void 特化实现
inline MFuture<void> MPromise<void>::GetFuture()
{
    std::lock_guard<std::mutex> Lock(State->Mutex);
    if (State->FutureRetrieved)
    {
        return MFuture<void>();
    }
    State->FutureRetrieved = true;
    return MFuture<void>(State);
}

inline void MPromise<void>::SetValue()
{
    TVector<TFunction<void(MFuture<void>)>> Callbacks;
    {
        std::lock_guard<std::mutex> Lock(State->Mutex);
        if (State->Ready)
        {
            return;
        }
        State->Ready = true;
        Callbacks = std::move(State->ThenCallbacks);
    }
    State->Cond.notify_all();
    for (auto& Callback : Callbacks)
    {
        if (Callback)
        {
            Callback(MFuture<void>(State));
        }
    }
}

inline void MPromise<void>::SetException(std::exception_ptr E)
{
    TVector<TFunction<void(MFuture<void>)>> Callbacks;
    {
        std::lock_guard<std::mutex> Lock(State->Mutex);
        if (State->Ready)
        {
            return;
        }
        State->Exception = std::move(E);
        State->Ready = true;
        Callbacks = std::move(State->ThenCallbacks);
    }
    State->Cond.notify_all();
    for (auto& Callback : Callbacks)
    {
        if (Callback)
        {
            Callback(MFuture<void>(State));
        }
    }
}

inline bool MFuture<void>::IsReady() const
{
    if (!State)
    {
        return false;
    }

    std::lock_guard<std::mutex> Lock(State->Mutex);
    return State->Ready;
}

inline void MFuture<void>::Wait() const
{
    if (!State)
    {
        throw std::runtime_error("Await on invalid MFuture");
    }
    std::unique_lock<std::mutex> Lock(State->Mutex);
    State->Cond.wait(Lock, [this]() { return State->Ready; });
}

inline void MFuture<void>::Await() const
{
    Wait();
    if (State->Exception)
    {
        std::rethrow_exception(State->Exception);
    }
}

inline void MFuture<void>::Get() const
{
    Await();
}

inline void MFuture<void>::Then(TFunction<void(MFuture<void>)> Callback)
{
    if (!State || !Callback)
    {
        return;
    }
    bool bCallNow = false;
    {
        std::lock_guard<std::mutex> Lock(State->Mutex);
        if (State->Ready)
        {
            bCallNow = true;
        }
        else
        {
            State->ThenCallbacks.push_back(Callback);
        }
    }
    if (bCallNow)
    {
        Callback(MFuture<void>(State));
    }
}
