#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Concurrency/ITaskRunner.h"
#include "Common/Runtime/Concurrency/Promise.h"
#include "Common/Runtime/Concurrency/ThreadPool.h"

#include <stdexcept>
#include <type_traits>

#ifdef Yield
    #undef Yield
#endif

namespace MAsync
{

/** 将 Next 投递到 Runner 的下一 tick 执行（即“让出”）。Runner 可为任意实现 ITaskRunner 的循环（如 MNetEventLoop）。 */
inline void Yield(ITaskRunner* Runner, TFunction<void()> Next)
{
    if (Runner && Next)
    {
        Runner->PostTask(std::move(Next));
    }
}

/** 调度层：把同步函数投递到线程池执行，并返回对应的 Future。 */
template<typename TFunc, typename TResult = std::invoke_result_t<std::decay_t<TFunc>>>
MFuture<TResult> Run(MThreadPool& Pool, TFunc&& Func)
{
    MPromise<TResult> Promise;
    MFuture<TResult> Future = Promise.GetFuture();
    const bool bAccepted = Pool.Submit(
        [Promise, Func = std::forward<TFunc>(Func)]() mutable
        {
            try
            {
                if constexpr (std::is_void_v<TResult>)
                {
                    std::invoke(Func);
                    Promise.SetValue();
                }
                else
                {
                    Promise.SetValue(std::invoke(Func));
                }
            }
            catch (...)
            {
                Promise.SetException(std::current_exception());
            }
        });

    if (!bAccepted)
    {
        Promise.SetException(std::make_exception_ptr(std::runtime_error("MThreadPool rejected async task")));
    }

    return Future;
}

/** 调度层：把同步函数投递到指定 Runner 的下一 tick 执行，并返回对应的 Future。 */
template<typename TFunc, typename TResult = std::invoke_result_t<std::decay_t<TFunc>>>
MFuture<TResult> Post(ITaskRunner* Runner, TFunc&& Func)
{
    MPromise<TResult> Promise;
    MFuture<TResult> Future = Promise.GetFuture();

    if (!Runner)
    {
        Promise.SetException(std::make_exception_ptr(std::runtime_error("ITaskRunner is null")));
        return Future;
    }

    Runner->PostTask(
        [Promise, Func = std::forward<TFunc>(Func)]() mutable
        {
            try
            {
                if constexpr (std::is_void_v<TResult>)
                {
                    std::invoke(Func);
                    Promise.SetValue();
                }
                else
                {
                    Promise.SetValue(std::invoke(Func));
                }
            }
            catch (...)
            {
                Promise.SetException(std::current_exception());
            }
        });

    return Future;
}

}
