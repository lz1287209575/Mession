#pragma once

#include "Common/Runtime/Concurrency/Async.h"
#include "Common/Runtime/Concurrency/Promise.h"
#include "Common/Runtime/Object/Result.h"
#include "Protocol/Messages/Common/AppMessages.h"

#include <exception>
#include <functional>
#include <type_traits>
#include <utility>

namespace MAppResultFutureSupport
{
template<typename TResultType>
MFuture<TResultType> MakeReadyFuture(TResultType Value)
{
    MPromise<TResultType> Promise;
    MFuture<TResultType> Future = Promise.GetFuture();
    Promise.SetValue(std::move(Value));
    return Future;
}

template<typename TResponse, typename TOnInvalid, typename TOnResolved, typename TOnException>
bool ObserveResultFuture(
    MFuture<TResult<TResponse, FAppError>> Future,
    TOnInvalid&& OnInvalid,
    TOnResolved&& OnResolved,
    TOnException&& OnException)
{
    if (!Future.Valid())
    {
        return std::invoke(std::forward<TOnInvalid>(OnInvalid));
    }

    Future.Then(
        [OnResolved = std::forward<TOnResolved>(OnResolved), OnException = std::forward<TOnException>(OnException)](
            MFuture<TResult<TResponse, FAppError>> Completed) mutable
        {
            try
            {
                std::invoke(OnResolved, Completed.Get());
            }
            catch (const std::exception& Ex)
            {
                std::invoke(OnException, Ex.what());
            }
            catch (...)
            {
                std::invoke(OnException, "unknown");
            }
        });

    return true;
}

template<typename TFuture>
struct TResultFutureTraits;

template<typename TResponse>
struct TResultFutureTraits<MFuture<TResult<TResponse, FAppError>>>
{
    using TResponseType = TResponse;
};

template<
    typename TInputResponse,
    typename TMapper,
    typename TInvalidFactory,
    typename TExceptionMapper,
    typename TOutputResponse = std::decay_t<std::invoke_result_t<std::decay_t<TMapper>, const TInputResponse&>>>
MFuture<TResult<TOutputResponse, FAppError>> Map(
    MFuture<TResult<TInputResponse, FAppError>> Future,
    TMapper&& Mapper,
    TInvalidFactory&& InvalidFactory,
    TExceptionMapper&& ExceptionMapper)
{
    if (!Future.Valid())
    {
        return std::invoke(std::forward<TInvalidFactory>(InvalidFactory), "invalid_future");
    }

    MPromise<TResult<TOutputResponse, FAppError>> Promise;
    MFuture<TResult<TOutputResponse, FAppError>> OutFuture = Promise.GetFuture();
    Future.Then(
        [Promise, Mapper = std::forward<TMapper>(Mapper), ExceptionMapper = std::forward<TExceptionMapper>(ExceptionMapper)](
            MFuture<TResult<TInputResponse, FAppError>> Completed) mutable
        {
            try
            {
                const TResult<TInputResponse, FAppError> Result = Completed.Get();
                if (!Result.IsOk())
                {
                    Promise.SetValue(MakeErrorResult<TOutputResponse>(Result.GetError()));
                    return;
                }

                Promise.SetValue(TResult<TOutputResponse, FAppError>::Ok(std::invoke(Mapper, Result.GetValue())));
            }
            catch (const std::exception& Ex)
            {
                Promise.SetValue(MakeErrorResult<TOutputResponse>(std::invoke(ExceptionMapper, Ex.what())));
            }
            catch (...)
            {
                Promise.SetValue(MakeErrorResult<TOutputResponse>(std::invoke(ExceptionMapper, "unknown")));
            }
        });
    return OutFuture;
}

template<typename TResponse, typename TObserver>
MFuture<TResult<TResponse, FAppError>> TapSuccess(
    MFuture<TResult<TResponse, FAppError>> Future,
    TObserver&& Observer)
{
    if (!Future.Valid())
    {
        return Future;
    }

    Future.Then(
        [Observer = std::forward<TObserver>(Observer)](MFuture<TResult<TResponse, FAppError>> Completed) mutable
        {
            try
            {
                const TResult<TResponse, FAppError> Result = Completed.Get();
                if (Result.IsOk())
                {
                    std::invoke(Observer, Result.GetValue());
                }
            }
            catch (...)
            {
            }
        });
    return Future;
}

template<
    typename TInputResponse,
    typename TBinder,
    typename TInvalidFactory,
    typename TExceptionMapper,
    typename TNextFuture = std::decay_t<std::invoke_result_t<std::decay_t<TBinder>, const TInputResponse&>>,
    typename TOutputResponse = typename TResultFutureTraits<TNextFuture>::TResponseType>
MFuture<TResult<TOutputResponse, FAppError>> Chain(
    MFuture<TResult<TInputResponse, FAppError>> Future,
    TBinder&& Binder,
    TInvalidFactory&& InvalidFactory,
    TExceptionMapper&& ExceptionMapper)
{
    if (!Future.Valid())
    {
        return std::invoke(std::forward<TInvalidFactory>(InvalidFactory), "invalid_future");
    }

    MPromise<TResult<TOutputResponse, FAppError>> Promise;
    MFuture<TResult<TOutputResponse, FAppError>> OutFuture = Promise.GetFuture();
    Future.Then(
        [Promise, Binder = std::forward<TBinder>(Binder), ExceptionMapper = std::forward<TExceptionMapper>(ExceptionMapper)](
            MFuture<TResult<TInputResponse, FAppError>> Completed) mutable
        {
            try
            {
                const TResult<TInputResponse, FAppError> Result = Completed.Get();
                if (!Result.IsOk())
                {
                    Promise.SetValue(MakeErrorResult<TOutputResponse>(Result.GetError()));
                    return;
                }

                TNextFuture NextFuture = std::invoke(Binder, Result.GetValue());
                if (!NextFuture.Valid())
                {
                    Promise.SetValue(MakeErrorResult<TOutputResponse>(std::invoke(ExceptionMapper, "invalid_chained_future")));
                    return;
                }

                NextFuture.Then(
                    [Promise, ExceptionMapper](TNextFuture NextCompleted) mutable
                    {
                        try
                        {
                            Promise.SetValue(NextCompleted.Get());
                        }
                        catch (const std::exception& Ex)
                        {
                            Promise.SetValue(MakeErrorResult<TOutputResponse>(std::invoke(ExceptionMapper, Ex.what())));
                        }
                        catch (...)
                        {
                            Promise.SetValue(MakeErrorResult<TOutputResponse>(std::invoke(ExceptionMapper, "unknown")));
                        }
                    });
            }
            catch (const std::exception& Ex)
            {
                Promise.SetValue(MakeErrorResult<TOutputResponse>(std::invoke(ExceptionMapper, Ex.what())));
            }
            catch (...)
            {
                Promise.SetValue(MakeErrorResult<TOutputResponse>(std::invoke(ExceptionMapper, "unknown")));
            }
        });
    return OutFuture;
}

template<
    typename TInputResponse,
    typename TMapper,
    typename TMakeInvalidFuture,
    typename TMakeAsyncExceptionError,
    typename TOutputResponse = std::decay_t<std::invoke_result_t<std::decay_t<TMapper>, const TInputResponse&>>>
MFuture<TResult<TOutputResponse, FAppError>> MapWithErrorPolicy(
    MFuture<TResult<TInputResponse, FAppError>> Future,
    TMapper&& Mapper,
    TMakeInvalidFuture&& MakeInvalidFuture,
    TMakeAsyncExceptionError&& MakeAsyncExceptionError)
{
    return Map(
        std::move(Future),
        std::forward<TMapper>(Mapper),
        [MakeInvalidFuture = std::forward<TMakeInvalidFuture>(MakeInvalidFuture)](const char* Message) mutable
        {
            return std::invoke(MakeInvalidFuture, Message ? Message : "invalid_future");
        },
        [MakeAsyncExceptionError = std::forward<TMakeAsyncExceptionError>(MakeAsyncExceptionError)](const char* Message) mutable
        {
            return std::invoke(MakeAsyncExceptionError, Message);
        });
}

template<
    typename TInputResponse,
    typename TBinder,
    typename TMakeInvalidFuture,
    typename TMakeAsyncExceptionError,
    typename TNextFuture = std::decay_t<std::invoke_result_t<std::decay_t<TBinder>, const TInputResponse&>>,
    typename TOutputResponse = typename TResultFutureTraits<TNextFuture>::TResponseType>
MFuture<TResult<TOutputResponse, FAppError>> ChainWithErrorPolicy(
    MFuture<TResult<TInputResponse, FAppError>> Future,
    TBinder&& Binder,
    TMakeInvalidFuture&& MakeInvalidFuture,
    TMakeAsyncExceptionError&& MakeAsyncExceptionError)
{
    return Chain(
        std::move(Future),
        std::forward<TBinder>(Binder),
        [MakeInvalidFuture = std::forward<TMakeInvalidFuture>(MakeInvalidFuture)](const char* Message) mutable
        {
            return std::invoke(MakeInvalidFuture, Message ? Message : "invalid_future");
        },
        [MakeAsyncExceptionError = std::forward<TMakeAsyncExceptionError>(MakeAsyncExceptionError)](const char* Message) mutable
        {
            return std::invoke(MakeAsyncExceptionError, Message);
        });
}
}
