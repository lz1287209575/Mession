#pragma once

#include "Common/Runtime/Async/MAsync.h"
#include "Common/Runtime/Concurrency/Promise.h"
#include "Common/Runtime/Object/Result.h"
#include "Protocol/Messages/Common/AppMessages.h"

#include <stdexcept>

// NOTE: legacy ucontext-backed path; new code should use the spec §7
// state-machine async/await model. See Docs/superpowers/specs/2026-07-24-cpp17-async-await.md.
// P4 (spec 2026-07-28 §C): MAwait / MAwaitOk / TPlayerCommandFuture deleted
// (2026-07-28). The remaining surface here is the player-command runtime
// plumbing (MHasCurrentPlayerCommand / MCurrentPlayerCommand / MCheckPoint /
// MYield / MPlayerCommandDetail::SuspendCurrentCommandUntil), which is
// kept in place for callers that legitimately run inside MFiberScheduler.

class MPlayerCommandContext;

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
