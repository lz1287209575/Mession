#include "Common/Runtime/Concurrency/FiberScheduler.h"

#include <stdexcept>
#if !defined(_WIN32)
#include <ucontext.h>
#endif

namespace
{
thread_local MFiberExecution* GCurrentFiberExecution = nullptr;

class FScopedCurrentFiberExecution
{
public:
    explicit FScopedCurrentFiberExecution(MFiberExecution* InExecution)
        : Previous(GCurrentFiberExecution)
    {
        GCurrentFiberExecution = InExecution;
    }

    ~FScopedCurrentFiberExecution()
    {
        GCurrentFiberExecution = Previous;
    }

private:
    MFiberExecution* Previous = nullptr;
};

#if defined(_WIN32)
class FNullFiberBackendExecution final : public IFiberBackendExecution
{
public:
    explicit FNullFiberBackendExecution(TFunction<void()> InEntryThunk)
        : EntryThunk(std::move(InEntryThunk))
    {
    }

    void SwitchIn() override
    {
        if (bHasRun)
        {
            // TODO: replace with real stackful resume semantics.
            throw std::runtime_error("Fiber resume is not implemented by the null backend");
        }

        bHasRun = true;
        if (EntryThunk)
        {
            EntryThunk();
        }
    }

    void SwitchOut() override
    {
        // TODO: replace with real stackful suspend semantics.
        throw std::runtime_error("Fiber suspend is not implemented by the null backend");
    }

private:
    TFunction<void()> EntryThunk;
    bool bHasRun = false;
};

class FNullFiberBackend final : public IFiberBackend
{
public:
    bool SupportsSuspendResume() const override
    {
        return false;
    }

    const char* GetDebugName() const override
    {
        return "windows_null";
    }

    TUniquePtr<IFiberBackendExecution> CreateExecution(
        size_t /*StackSize*/,
        TFunction<void()> EntryThunk) override
    {
        return std::make_unique<FNullFiberBackendExecution>(std::move(EntryThunk));
    }
};
#else
class FPosixFiberBackendExecution final : public IFiberBackendExecution
{
public:
    FPosixFiberBackendExecution(size_t InStackSize, TFunction<void()> InEntryThunk)
        : EntryThunk(std::move(InEntryThunk))
        , StackSize(InStackSize > 0 ? InStackSize : 256 * 1024)
        , Stack(new char[StackSize])
    {
        if (getcontext(&FiberContext) != 0)
        {
            throw std::runtime_error("getcontext failed while creating fiber");
        }

        FiberContext.uc_stack.ss_sp = Stack.get();
        FiberContext.uc_stack.ss_size = StackSize;
        FiberContext.uc_link = nullptr;

        const uintptr_t RawThis = reinterpret_cast<uintptr_t>(this);
        const uint32 Low = static_cast<uint32>(RawThis & 0xffffffffu);
        const uint32 High = static_cast<uint32>((RawThis >> 32u) & 0xffffffffu);
        makecontext(
            &FiberContext,
            reinterpret_cast<void (*)()>(&FPosixFiberBackendExecution::Trampoline),
            2,
            Low,
            High);
    }

    void SwitchIn() override
    {
        if (bCompleted)
        {
            return;
        }

        if (swapcontext(&CallerContext, &FiberContext) != 0)
        {
            throw std::runtime_error("swapcontext failed while entering fiber");
        }
    }

    void SwitchOut() override
    {
        if (swapcontext(&FiberContext, &CallerContext) != 0)
        {
            throw std::runtime_error("swapcontext failed while suspending fiber");
        }
    }

private:
    static void Trampoline(uint32 Low, uint32 High)
    {
        const uintptr_t RawThis =
            (static_cast<uintptr_t>(High) << 32u) | static_cast<uintptr_t>(Low);
        FPosixFiberBackendExecution* Self = reinterpret_cast<FPosixFiberBackendExecution*>(RawThis);
        if (!Self)
        {
            throw std::runtime_error("Fiber trampoline received null execution");
        }

        if (Self->EntryThunk)
        {
            Self->EntryThunk();
        }

        Self->bCompleted = true;
        if (setcontext(&Self->CallerContext) != 0)
        {
            throw std::runtime_error("setcontext failed while leaving completed fiber");
        }
    }

private:
    TFunction<void()> EntryThunk;
    size_t StackSize = 0;
    TUniquePtr<char[]> Stack;
    ucontext_t FiberContext {};
    ucontext_t CallerContext {};
    bool bCompleted = false;
};

class FPosixFiberBackend final : public IFiberBackend
{
public:
    bool SupportsSuspendResume() const override
    {
        return true;
    }

    const char* GetDebugName() const override
    {
        return "posix_ucontext";
    }

    TUniquePtr<IFiberBackendExecution> CreateExecution(
        size_t StackSize,
        TFunction<void()> EntryThunk) override
    {
        return std::make_unique<FPosixFiberBackendExecution>(StackSize, std::move(EntryThunk));
    }
};
#endif
}

MFiberScheduler& MFiberScheduler::Get()
{
    static MFiberScheduler Scheduler;
    return Scheduler;
}

MFiberScheduler::MFiberScheduler()
#if defined(_WIN32)
    : Backend(std::make_unique<FNullFiberBackend>())
#else
    : Backend(std::make_unique<FPosixFiberBackend>())
#endif
{
}

TSharedPtr<MFiberExecution> MFiberScheduler::CreateExecution(SFiberStartParams Params)
{
    if (!Params.Entry)
    {
        return nullptr;
    }

    const uint64 ExecutionId = NextExecutionId.fetch_add(1);
    TSharedPtr<MFiberExecution> Execution(new MFiberExecution(
        MFiberExecution::SPrivateTag {},
        ExecutionId,
        std::move(Params.CommandState),
        std::move(Params.Context)));

    Execution->Entry = std::move(Params.Entry);
    Execution->OnCompleted = std::move(Params.OnCompleted);
    Execution->OnUnhandledException = std::move(Params.OnUnhandledException);
    Execution->BackendExecution = Backend->CreateExecution(
        Params.StackSize,
        [this, WeakExecution = TWeakPtr<MFiberExecution>(Execution)]()
        {
            TSharedPtr<MFiberExecution> StrongExecution = WeakExecution.lock();
            if (!StrongExecution)
            {
                return;
            }

            RunExecutionEntry(*StrongExecution);
        });

    return Execution;
}

void MFiberScheduler::Start(const TSharedPtr<MFiberExecution>& Execution)
{
    if (!Execution || !Execution->BackendExecution)
    {
        throw std::runtime_error("Invalid fiber execution");
    }

    if (Execution->GetState() != EFiberExecutionState::Created)
    {
        throw std::runtime_error("Fiber execution must start from Created state");
    }

    Execution->State.store(EFiberExecutionState::Running);
    Execution->BackendExecution->SwitchIn();
}

void MFiberScheduler::Resume(const TSharedPtr<MFiberExecution>& Execution)
{
    if (!Execution || !Execution->BackendExecution)
    {
        return;
    }

    if (Execution->IsCompleted())
    {
        return;
    }

    if (Execution->GetState() != EFiberExecutionState::Suspended)
    {
        throw std::runtime_error("Fiber execution must be suspended before resume");
    }

    Execution->State.store(EFiberExecutionState::Running);
    Execution->BackendExecution->SwitchIn();
}

void MFiberScheduler::SuspendCurrent()
{
    MFiberExecution* Current = GetCurrentExecution();
    if (!Current || !Current->BackendExecution)
    {
        throw std::runtime_error("No active fiber execution");
    }

    Current->State.store(EFiberExecutionState::Suspended);
    Current->BackendExecution->SwitchOut();
}

bool MFiberScheduler::SupportsSuspendResume() const
{
    return Backend && Backend->SupportsSuspendResume();
}

const char* MFiberScheduler::GetBackendName() const
{
    return Backend ? Backend->GetDebugName() : "missing";
}

MFiberExecution* MFiberScheduler::GetCurrentExecution() const
{
    return GCurrentFiberExecution;
}

void MFiberScheduler::RunExecutionEntry(MFiberExecution& Execution)
{
    FScopedCurrentFiberExecution Scope(&Execution);

    try
    {
        Execution.State.store(EFiberExecutionState::Running);
        Execution.Entry();

        Execution.State.store(EFiberExecutionState::Completed);
        if (Execution.OnCompleted)
        {
            Execution.OnCompleted();
        }
    }
    catch (...)
    {
        Execution.State.store(EFiberExecutionState::Failed);
        if (Execution.OnUnhandledException)
        {
            Execution.OnUnhandledException(std::current_exception());
        }
        else
        {
            throw;
        }
    }
}
