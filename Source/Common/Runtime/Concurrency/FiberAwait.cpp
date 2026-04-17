#include "Common/Runtime/Concurrency/FiberAwait.h"

#include "Common/Runtime/Concurrency/FiberScheduler.h"
#include "Servers/World/Player/PlayerCommandRuntime.h"

namespace
{
struct FResumeGateState
{
    std::mutex Mutex;
    bool bResumeRequested = false;
    bool bSuspended = false;
    bool bResumed = false;
};
}

bool MHasCurrentPlayerCommand()
{
    return MFiberScheduler::Get().GetCurrentExecution() != nullptr;
}

MPlayerCommandContext& MCurrentPlayerCommand()
{
    MFiberExecution* Execution = MFiberScheduler::Get().GetCurrentExecution();
    if (!Execution)
    {
        throw std::runtime_error("No active player command");
    }

    return Execution->GetContext();
}

void MCheckPoint()
{
    if (!MHasCurrentPlayerCommand())
    {
        return;
    }

    MPlayerCommandDetail::CheckPointOrAbort();
}

void MYield()
{
    if (!MHasCurrentPlayerCommand())
    {
        return;
    }

    MPlayerCommandDetail::YieldCurrentCommand();
}

namespace MPlayerCommandDetail
{
void SuspendCurrentCommandUntil(const TFunction<void(TFunction<void()>)>& Registrar)
{
    if (!Registrar)
    {
        throw std::runtime_error("SuspendCurrentCommandUntil requires registrar");
    }

    MFiberExecution* CurrentExecution = MFiberScheduler::Get().GetCurrentExecution();
    if (!CurrentExecution)
    {
        throw std::runtime_error("SuspendCurrentCommandUntil requires active player command");
    }

    if (!MFiberScheduler::Get().SupportsSuspendResume())
    {
        const char* BackendName = MFiberScheduler::Get().GetBackendName();
        const MString Message =
            MString("fiber suspend/resume is not supported by backend: ") +
            (BackendName ? BackendName : "unknown");
        if (CurrentExecution->GetContext().GetRuntime())
        {
            throw FPlayerCommandError(FAppError::Make("fiber_backend_unsupported", Message));
        }

        throw std::runtime_error(Message);
    }

    TSharedPtr<MFiberExecution> Execution = CurrentExecution->shared_from_this();
    ITaskRunner* Runner = CurrentExecution->GetContext().GetRunner();
    TSharedPtr<FResumeGateState> Gate = MakeShared<FResumeGateState>();

    Registrar(
        [Gate, WeakExecution = TWeakPtr<MFiberExecution>(Execution), Runner]() mutable
        {
            TFunction<void()> ResumeTask =
                [Gate, WeakExecution]() mutable
                {
                    TSharedPtr<MFiberExecution> StrongExecution = WeakExecution.lock();
                    if (!StrongExecution)
                    {
                        return;
                    }

                    bool bShouldResume = false;
                    {
                        std::lock_guard<std::mutex> Lock(Gate->Mutex);
                        if (Gate->bResumed)
                        {
                            return;
                        }

                        if (!Gate->bSuspended)
                        {
                            Gate->bResumeRequested = true;
                            return;
                        }

                        Gate->bResumed = true;
                        bShouldResume = true;
                    }

                    if (bShouldResume)
                    {
                        MFiberScheduler::Get().Resume(StrongExecution);
                    }
                };

            if (Runner)
            {
                Runner->PostTask(std::move(ResumeTask));
                return;
            }

            ResumeTask();
        });

    {
        std::lock_guard<std::mutex> Lock(Gate->Mutex);
        if (Gate->bResumeRequested)
        {
            Gate->bResumed = true;
            return;
        }

        Gate->bSuspended = true;
    }

    MFiberScheduler::Get().SuspendCurrent();
}

void CheckPointOrAbort()
{
    MPlayerCommandContext& Context = MCurrentPlayerCommand();
    MPlayerCommandRuntime* Runtime = Context.GetRuntime();
    if (!Runtime)
    {
        return;
    }

    for (const SPlayerCommandEpoch& Participant : Context.GetParticipants())
    {
        const uint64 CurrentEpoch = Runtime->SnapshotEpoch(Participant.PlayerId);
        if (CurrentEpoch == Participant.Epoch)
        {
            continue;
        }

        throw FPlayerCommandAbort(
            Participant.PlayerId,
            Participant.Epoch,
            "player command became stale after epoch change");
    }
}

void YieldCurrentCommand()
{
    ITaskRunner* Runner = MCurrentPlayerCommand().GetRunner();
    SuspendCurrentCommandUntil(
        [Runner](TFunction<void()> Resume) mutable
        {
            if (Runner)
            {
                Runner->PostTask(std::move(Resume));
                return;
            }

            Resume();
        });
}
}
