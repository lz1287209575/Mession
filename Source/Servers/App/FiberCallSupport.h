#pragma once

#include "Common/Runtime/Concurrency/FiberScheduler.h"

#include <exception>

namespace MAppFiberCallSupport
{
enum class EFiberFailureKind : uint8
{
    RunnerMissing,
    BodyException,
    UnhandledException,
    CreateFailed,
    StartFailed,
};

template<typename TResultValue, typename TBody, typename TOnCompleted, typename TOnFailure>
bool StartDetachedResultFiber(
    const char* CommandName,
    ITaskRunner* Runner,
    TBody&& Body,
    TOnCompleted&& OnCompleted,
    TOnFailure&& OnFailure)
{
    if (!Runner)
    {
        std::invoke(std::forward<TOnFailure>(OnFailure), EFiberFailureKind::RunnerMissing, "task_runner_missing");
        return false;
    }

    using TBodyValue = std::decay_t<TBody>;
    using TOnCompletedValue = std::decay_t<TOnCompleted>;
    using TOnFailureValue = std::decay_t<TOnFailure>;

    TBodyValue BodyValue(std::forward<TBody>(Body));
    TOnCompletedValue OnCompletedValue(std::forward<TOnCompleted>(OnCompleted));
    TOnFailureValue OnFailureValue(std::forward<TOnFailure>(OnFailure));

    TSharedPtr<MFiberExecution> Execution = MFiberScheduler::Get().CreateExecution(
        {
            nullptr,
            MPlayerCommandContext({}, CommandName ? CommandName : "DetachedFiber", Runner, nullptr),
            [Body = std::move(BodyValue), OnCompleted = OnCompletedValue, OnFailure = OnFailureValue]() mutable
            {
                try
                {
                    std::invoke(OnCompleted, TResultValue(Body()));
                }
                catch (const std::exception& Ex)
                {
                    std::invoke(OnFailure, EFiberFailureKind::BodyException, Ex.what());
                }
                catch (...)
                {
                    std::invoke(OnFailure, EFiberFailureKind::BodyException, "unknown");
                }
            },
            []()
            {
            },
            [OnFailure = OnFailureValue](std::exception_ptr Exception) mutable
            {
                try
                {
                    if (Exception)
                    {
                        std::rethrow_exception(Exception);
                    }

                    std::invoke(OnFailure, EFiberFailureKind::UnhandledException, "unknown");
                }
                catch (const std::exception& Ex)
                {
                    std::invoke(OnFailure, EFiberFailureKind::UnhandledException, Ex.what());
                }
                catch (...)
                {
                    std::invoke(OnFailure, EFiberFailureKind::UnhandledException, "unknown");
                }
            },
        });

    if (!Execution)
    {
        std::invoke(OnFailureValue, EFiberFailureKind::CreateFailed, "fiber_create_failed");
        return false;
    }

    Runner->PostTask(
        [Execution, OnFailure = std::move(OnFailureValue)]() mutable
        {
            try
            {
                MFiberScheduler::Get().Start(Execution);
            }
            catch (const std::exception& Ex)
            {
                std::invoke(OnFailure, EFiberFailureKind::StartFailed, Ex.what());
            }
            catch (...)
            {
                std::invoke(OnFailure, EFiberFailureKind::StartFailed, "unknown");
            }
        });
    return true;
}
}
