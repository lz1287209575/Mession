#pragma once

#include "Common/Runtime/Concurrency/Promise.h"
#include "Common/Runtime/Object/Result.h"
#include "Protocol/Messages/Common/AppMessages.h"

#include <stdexcept>

class MPlayerCommandContext;

template<typename T>
using TPlayerCommandFuture = MFuture<TResult<T, FAppError>>;

// True when executing inside PlayerCommandRuntime-managed fiber context.
bool MHasCurrentPlayerCommand();

// Returns the active player command context for the current fiber.
MPlayerCommandContext& MCurrentPlayerCommand();

// Checks whether the current command is still valid for the captured epoch.
void MCheckPoint();

// Suspends the current command once and posts its continuation back to the
// owning player strand.
void MYield();

namespace MPlayerCommandDetail
{
MString BuildAwaitErrorMessage(const FAppError& Error);

class FPlayerCommandAbort : public std::exception
{
public:
    FPlayerCommandAbort(uint64 InPlayerId, uint64 InEpoch, MString InMessage)
        : PlayerId(InPlayerId)
        , Epoch(InEpoch)
        , Message(std::move(InMessage))
    {
    }

    const char* what() const noexcept override
    {
        return Message.c_str();
    }

    uint64 GetPlayerId() const
    {
        return PlayerId;
    }

    uint64 GetEpoch() const
    {
        return Epoch;
    }

private:
    uint64 PlayerId = 0;
    uint64 Epoch = 0;
    MString Message;
};

class FPlayerCommandError : public std::exception
{
public:
    explicit FPlayerCommandError(FAppError InError)
        : Error(std::move(InError))
        , Message(BuildAwaitErrorMessage(Error))
    {
    }

    const char* what() const noexcept override
    {
        return Message.c_str();
    }

    const FAppError& GetError() const
    {
        return Error;
    }

private:
    FAppError Error;
    MString Message;
};

// Registers a resume callback and suspends the current command until it is resumed.
void SuspendCurrentCommandUntil(const TFunction<void(TFunction<void()>)>& Registrar);

// Throws FPlayerCommandAbort when the current command becomes stale.
void CheckPointOrAbort();

// Internal implementation for MYield.
void YieldCurrentCommand();
}

template<typename T>
T MAwait(MFuture<T> Future);

void MAwait(MFuture<void> Future);

template<typename T>
T MAwaitOk(TPlayerCommandFuture<T> Future);

void MAwaitOk(TPlayerCommandFuture<void> Future);

namespace MPlayerCommandDetail
{
inline MString BuildAwaitErrorMessage(const FAppError& Error)
{
    if (Error.Code.empty())
    {
        return Error.Message.empty() ? "player_command_failed" : Error.Message;
    }

    if (Error.Message.empty())
    {
        return Error.Code;
    }

    return Error.Code + ": " + Error.Message;
}
}

template<typename T>
T MAwait(MFuture<T> Future)
{
    if (!Future.Valid())
    {
        throw std::runtime_error("Await on invalid MFuture");
    }

    if (!MHasCurrentPlayerCommand() || Future.IsReady())
    {
        return Future.Get();
    }

    struct SAwaitState
    {
        TOptional<T> Value;
        std::exception_ptr Exception;
    };

    TSharedPtr<SAwaitState> State = MakeShared<SAwaitState>();
    MPlayerCommandDetail::SuspendCurrentCommandUntil(
        [State, Future = std::move(Future)](TFunction<void()> Resume) mutable
        {
            Future.Then(
                [State, Resume = std::move(Resume)](MFuture<T> Completed) mutable
                {
                    try
                    {
                        State->Value = Completed.Get();
                    }
                    catch (...)
                    {
                        State->Exception = std::current_exception();
                    }

                    Resume();
                });
        });

    MPlayerCommandDetail::CheckPointOrAbort();

    if (State->Exception)
    {
        std::rethrow_exception(State->Exception);
    }

    if (!State->Value.has_value())
    {
        throw std::runtime_error("Fiber await completed without value");
    }

    return std::move(*State->Value);
}

inline void MAwait(MFuture<void> Future)
{
    if (!Future.Valid())
    {
        throw std::runtime_error("Await on invalid MFuture");
    }

    if (!MHasCurrentPlayerCommand() || Future.IsReady())
    {
        Future.Get();
        return;
    }

    struct SAwaitState
    {
        std::exception_ptr Exception;
    };

    TSharedPtr<SAwaitState> State = MakeShared<SAwaitState>();
    MPlayerCommandDetail::SuspendCurrentCommandUntil(
        [State, Future = std::move(Future)](TFunction<void()> Resume) mutable
        {
            Future.Then(
                [State, Resume = std::move(Resume)](MFuture<void> Completed) mutable
                {
                    try
                    {
                        Completed.Get();
                    }
                    catch (...)
                    {
                        State->Exception = std::current_exception();
                    }

                    Resume();
                });
        });

    MPlayerCommandDetail::CheckPointOrAbort();

    if (State->Exception)
    {
        std::rethrow_exception(State->Exception);
    }
}

template<typename T>
T MAwaitOk(TPlayerCommandFuture<T> Future)
{
    TResult<T, FAppError> Result = MAwait(std::move(Future));
    if (Result.IsErr())
    {
        throw MPlayerCommandDetail::FPlayerCommandError(Result.GetError());
    }

    return std::move(Result.GetValue());
}

inline void MAwaitOk(TPlayerCommandFuture<void> Future)
{
    TResult<void, FAppError> Result = MAwait(std::move(Future));
    if (Result.IsErr())
    {
        throw MPlayerCommandDetail::FPlayerCommandError(Result.GetError());
    }
}
