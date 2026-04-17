#pragma once

#include "Common/Runtime/Concurrency/CommandExecutionContext.h"
#include "Common/Runtime/MLib.h"

#include <exception>

class MPlayerCommandState;

enum class EFiberExecutionState : uint8
{
    Created,
    Running,
    Suspended,
    Completed,
    Failed,
};

class IFiberBackendExecution
{
public:
    virtual ~IFiberBackendExecution() = default;

    virtual void SwitchIn() = 0;
    virtual void SwitchOut() = 0;
};

class IFiberBackend
{
public:
    virtual ~IFiberBackend() = default;

    virtual bool SupportsSuspendResume() const = 0;
    virtual const char* GetDebugName() const = 0;

    virtual TUniquePtr<IFiberBackendExecution> CreateExecution(
        size_t StackSize,
        TFunction<void()> EntryThunk) = 0;
};

class MFiberExecution : public TEnableSharedFromThis<MFiberExecution>
{
public:
    uint64 GetExecutionId() const
    {
        return ExecutionId;
    }

    EFiberExecutionState GetState() const
    {
        return State.load();
    }

    bool IsSuspended() const
    {
        return GetState() == EFiberExecutionState::Suspended;
    }

    bool IsCompleted() const
    {
        const EFiberExecutionState Value = GetState();
        return Value == EFiberExecutionState::Completed || Value == EFiberExecutionState::Failed;
    }

    const MPlayerCommandContext& GetContext() const
    {
        return Context;
    }

    MPlayerCommandContext& GetContext()
    {
        return Context;
    }

    const TSharedPtr<MPlayerCommandState>& GetCommandState() const
    {
        return CommandState;
    }

private:
    friend class MFiberScheduler;

    struct SPrivateTag
    {
    };

    MFiberExecution(
        SPrivateTag,
        uint64 InExecutionId,
        TSharedPtr<MPlayerCommandState> InCommandState,
        MPlayerCommandContext InContext)
        : ExecutionId(InExecutionId)
        , CommandState(std::move(InCommandState))
        , Context(std::move(InContext))
    {
    }

private:
    uint64 ExecutionId = 0;
    std::atomic<EFiberExecutionState> State { EFiberExecutionState::Created };

    TSharedPtr<MPlayerCommandState> CommandState;
    MPlayerCommandContext Context;

    TFunction<void()> Entry;
    TFunction<void()> OnCompleted;
    TFunction<void(std::exception_ptr)> OnUnhandledException;

    TUniquePtr<IFiberBackendExecution> BackendExecution;
};

struct SFiberStartParams
{
    TSharedPtr<MPlayerCommandState> CommandState;
    MPlayerCommandContext Context;
    TFunction<void()> Entry;
    TFunction<void()> OnCompleted;
    TFunction<void(std::exception_ptr)> OnUnhandledException;
    size_t StackSize = 256 * 1024;
};

class MFiberScheduler
{
public:
    static MFiberScheduler& Get();

    TSharedPtr<MFiberExecution> CreateExecution(SFiberStartParams Params);

    void Start(const TSharedPtr<MFiberExecution>& Execution);
    void Resume(const TSharedPtr<MFiberExecution>& Execution);
    void SuspendCurrent();

    bool SupportsSuspendResume() const;
    const char* GetBackendName() const;

    MFiberExecution* GetCurrentExecution() const;

private:
    MFiberScheduler();

    void RunExecutionEntry(MFiberExecution& Execution);

private:
    TUniquePtr<IFiberBackend> Backend;
    std::atomic<uint64> NextExecutionId { 1 };
};
