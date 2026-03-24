#pragma once

#include "Common/Runtime/Concurrency/Promise.h"

#include <atomic>

template<typename T>
class MCoroutine : public TEnableSharedFromThis<MCoroutine<T>>
{
public:
    /** Workflow object：封装多步异步流程，内部通过 Promise/Future 完成结果传播。 */
    using TResultType = T;

    MCoroutine()
        : Future(Promise.GetFuture())
    {
    }

    virtual ~MCoroutine() = default;

    MFuture<T> GetFuture() const
    {
        return Future;
    }

    bool IsStarted() const
    {
        return bStarted.load();
    }

    bool IsCompleted() const
    {
        return bCompleted.load();
    }

    void Start()
    {
        bool bExpected = false;
        if (!bStarted.compare_exchange_strong(bExpected, true))
        {
            return;
        }

        if (!LifetimeGuard)
        {
            try
            {
                LifetimeGuard = this->shared_from_this();
            }
            catch (const std::bad_weak_ptr&)
            {
            }
        }

        try
        {
            OnStart();
        }
        catch (...)
        {
            Reject(std::current_exception());
        }
    }

    /** 供 MAsync::StartCoroutine 调用：在协程完成前保持自身存活。 */
    void RetainUntilCompletion(TSharedPtr<MCoroutine<T>> InSelf)
    {
        LifetimeGuard = std::move(InSelf);
    }

protected:
    virtual void OnStart() = 0;

    void Resolve(const T& Value)
    {
        Complete([this, &Value]()
        {
            Promise.SetValue(Value);
        });
    }

    void Resolve(T&& Value)
    {
        Complete([this, &Value]()
        {
            Promise.SetValue(std::move(Value));
        });
    }

    void Reject(std::exception_ptr Exception)
    {
        Complete([this, &Exception]()
        {
            Promise.SetException(std::move(Exception));
        });
    }

private:
    template<typename TComplete>
    void Complete(TComplete&& CompleteAction)
    {
        bool bExpected = false;
        if (!bCompleted.compare_exchange_strong(bExpected, true))
        {
            return;
        }

        CompleteAction();
        LifetimeGuard.reset();
    }

    MPromise<T> Promise;
    MFuture<T> Future;
    TSharedPtr<MCoroutine<T>> LifetimeGuard;
    std::atomic<bool> bStarted { false };
    std::atomic<bool> bCompleted { false };
};

template<>
class MCoroutine<void> : public TEnableSharedFromThis<MCoroutine<void>>
{
public:
    /** Workflow object：封装多步异步流程，内部通过 Promise/Future 完成结果传播。 */
    using TResultType = void;

    MCoroutine()
        : Future(Promise.GetFuture())
    {
    }

    virtual ~MCoroutine() = default;

    MFuture<void> GetFuture() const
    {
        return Future;
    }

    bool IsStarted() const
    {
        return bStarted.load();
    }

    bool IsCompleted() const
    {
        return bCompleted.load();
    }

    void Start()
    {
        bool bExpected = false;
        if (!bStarted.compare_exchange_strong(bExpected, true))
        {
            return;
        }

        if (!LifetimeGuard)
        {
            try
            {
                LifetimeGuard = this->shared_from_this();
            }
            catch (const std::bad_weak_ptr&)
            {
            }
        }

        try
        {
            OnStart();
        }
        catch (...)
        {
            Reject(std::current_exception());
        }
    }

    /** 供 MAsync::StartCoroutine 调用：在协程完成前保持自身存活。 */
    void RetainUntilCompletion(TSharedPtr<MCoroutine<void>> InSelf)
    {
        LifetimeGuard = std::move(InSelf);
    }

protected:
    virtual void OnStart() = 0;

    void Resolve()
    {
        Complete([this]()
        {
            Promise.SetValue();
        });
    }

    void Reject(std::exception_ptr Exception)
    {
        Complete([this, &Exception]()
        {
            Promise.SetException(std::move(Exception));
        });
    }

private:
    template<typename TComplete>
    void Complete(TComplete&& CompleteAction)
    {
        bool bExpected = false;
        if (!bCompleted.compare_exchange_strong(bExpected, true))
        {
            return;
        }

        CompleteAction();
        LifetimeGuard.reset();
    }

    MPromise<void> Promise;
    MFuture<void> Future;
    TSharedPtr<MCoroutine<void>> LifetimeGuard;
    std::atomic<bool> bStarted { false };
    std::atomic<bool> bCompleted { false };
};
