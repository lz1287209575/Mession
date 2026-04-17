#pragma once

#include "Common/Runtime/Concurrency/CommandExecutionContext.h"
#include "Common/Runtime/Concurrency/FiberAwait.h"
#include "Common/Runtime/Concurrency/ITaskRunner.h"
#include "Common/Runtime/Concurrency/Promise.h"
#include "Common/Runtime/Object/Result.h"
#include "Protocol/Messages/Common/AppMessages.h"

#include <exception>
#include <tuple>

template<typename T>
using TPlayerCommandFuture = MFuture<TResult<T, FAppError>>;

class MPlayerStrand;
class MPlayerCommandState;
class MPlayerCommandRuntime;
class MFiberExecution;
using TPlayerCommandStart = TFunction<void()>;
using TPlayerCommandFailure = TFunction<void(FAppError)>;

struct SPlayerCommandParticipant
{
    uint64 PlayerId = 0;
    uint64 ExpectedEpoch = 0;
    bool bCreateStrandIfMissing = true;
};

struct SPlayerCommandOptions
{
    const char* CommandName = "";
    uint64 ExpectedEpoch = 0;
    bool bCreateStrandIfMissing = true;
};

class MPlayerCommandRuntime
{
public:
    explicit MPlayerCommandRuntime(ITaskRunner* InRunner)
        : Runner(InRunner)
    {
    }

    ITaskRunner* GetRunner() const
    {
        return Runner;
    }

    template<typename TResponse, typename TFunc>
    TPlayerCommandFuture<TResponse> Enqueue(
        uint64 PlayerId,
        const SPlayerCommandOptions& Options,
        TFunc&& Body)
    {
        MPromise<TResult<TResponse, FAppError>> Promise;
        TPlayerCommandFuture<TResponse> Future = Promise.GetFuture();

        TFunction<TResult<TResponse, FAppError>()> BodyFunc(std::forward<TFunc>(Body));
        const TOptional<FAppError> Error = EnqueuePrepared(
            PlayerId,
            Options,
            [Promise, Body = std::move(BodyFunc)]() mutable
            {
                try
                {
                    Promise.SetValue(Body());
                }
                catch (const MPlayerCommandDetail::FPlayerCommandError& Ex)
                {
                    Promise.SetValue(MakeErrorResult<TResponse>(Ex.GetError()));
                }
                catch (const MPlayerCommandDetail::FPlayerCommandAbort& Ex)
                {
                    Promise.SetValue(MakeErrorResult<TResponse>(
                        FAppError::Make("player_command_stale", Ex.what())));
                }
                catch (const std::exception& Ex)
                {
                    Promise.SetValue(MakeErrorResult<TResponse>(
                        FAppError::Make("player_command_exception", Ex.what())));
                }
                catch (...)
                {
                    Promise.SetValue(MakeErrorResult<TResponse>(
                        FAppError::Make("player_command_exception", "unknown")));
                }
            },
            [Promise](FAppError Error) mutable
            {
                Promise.SetValue(MakeErrorResult<TResponse>(std::move(Error)));
            });

        if (Error.has_value())
        {
            Promise.SetValue(MakeErrorResult<TResponse>(*Error));
        }

        return Future;
    }

    template<typename TResponse, typename TFunc>
    TPlayerCommandFuture<TResponse> EnqueuePair(
        uint64 PrimaryPlayerId,
        uint64 SecondaryPlayerId,
        const SPlayerCommandOptions& Options,
        TFunc&& Body)
    {
        TVector<SPlayerCommandParticipant> Participants;
        Participants.push_back(SPlayerCommandParticipant{
            PrimaryPlayerId,
            Options.ExpectedEpoch,
            Options.bCreateStrandIfMissing,
        });
        Participants.push_back(SPlayerCommandParticipant{
            SecondaryPlayerId,
            0,
            Options.bCreateStrandIfMissing,
        });
        return EnqueueMany<TResponse>(std::move(Participants), Options, std::forward<TFunc>(Body));
    }

    template<typename TResponse, typename TFunc>
    TPlayerCommandFuture<TResponse> EnqueueMany(
        TVector<SPlayerCommandParticipant> Participants,
        const SPlayerCommandOptions& Options,
        TFunc&& Body)
    {
        MPromise<TResult<TResponse, FAppError>> Promise;
        TPlayerCommandFuture<TResponse> Future = Promise.GetFuture();

        TFunction<TResult<TResponse, FAppError>()> BodyFunc(std::forward<TFunc>(Body));
        const TOptional<FAppError> Error = EnqueuePreparedForPlayers(
            std::move(Participants),
            Options,
            [Promise, Body = std::move(BodyFunc)]() mutable
            {
                try
                {
                    Promise.SetValue(Body());
                }
                catch (const MPlayerCommandDetail::FPlayerCommandError& Ex)
                {
                    Promise.SetValue(MakeErrorResult<TResponse>(Ex.GetError()));
                }
                catch (const MPlayerCommandDetail::FPlayerCommandAbort& Ex)
                {
                    Promise.SetValue(MakeErrorResult<TResponse>(
                        FAppError::Make("player_command_stale", Ex.what())));
                }
                catch (const std::exception& Ex)
                {
                    Promise.SetValue(MakeErrorResult<TResponse>(
                        FAppError::Make("player_command_exception", Ex.what())));
                }
                catch (...)
                {
                    Promise.SetValue(MakeErrorResult<TResponse>(
                        FAppError::Make("player_command_exception", "unknown")));
                }
            },
            [Promise](FAppError Error) mutable
            {
                Promise.SetValue(MakeErrorResult<TResponse>(std::move(Error)));
            });

        if (Error.has_value())
        {
            Promise.SetValue(MakeErrorResult<TResponse>(*Error));
        }

        return Future;
    }

    template<typename TObject, typename TResponse, typename... TArgs>
    TPlayerCommandFuture<TResponse> Enqueue(
        uint64 PlayerId,
        const SPlayerCommandOptions& Options,
        TObject* Object,
        TResult<TResponse, FAppError>(TObject::*Method)(TArgs...),
        TArgs&&... Args)
    {
        return Enqueue<TResponse>(
            PlayerId,
            Options,
            [Object, Method, Tuple = std::make_tuple(std::forward<TArgs>(Args)...)]() mutable -> TResult<TResponse, FAppError>
            {
                return std::apply(
                    [Object, Method](auto&&... UnpackedArgs) -> TResult<TResponse, FAppError>
                    {
                        return (Object->*Method)(std::forward<decltype(UnpackedArgs)>(UnpackedArgs)...);
                    },
                    std::move(Tuple));
            });
    }

    template<typename TObject, typename TResponse, typename... TArgs>
    TPlayerCommandFuture<TResponse> EnqueuePair(
        uint64 PrimaryPlayerId,
        uint64 SecondaryPlayerId,
        const SPlayerCommandOptions& Options,
        TObject* Object,
        TResult<TResponse, FAppError>(TObject::*Method)(TArgs...),
        TArgs&&... Args)
    {
        return EnqueuePair<TResponse>(
            PrimaryPlayerId,
            SecondaryPlayerId,
            Options,
            [Object, Method, Tuple = std::make_tuple(std::forward<TArgs>(Args)...)]() mutable -> TResult<TResponse, FAppError>
            {
                return std::apply(
                    [Object, Method](auto&&... UnpackedArgs) -> TResult<TResponse, FAppError>
                    {
                        return (Object->*Method)(std::forward<decltype(UnpackedArgs)>(UnpackedArgs)...);
                    },
                    std::move(Tuple));
            });
    }

    template<typename TObject, typename TResponse, typename... TArgs>
    TPlayerCommandFuture<TResponse> EnqueueMany(
        TVector<SPlayerCommandParticipant> Participants,
        const SPlayerCommandOptions& Options,
        TObject* Object,
        TResult<TResponse, FAppError>(TObject::*Method)(TArgs...),
        TArgs&&... Args)
    {
        return EnqueueMany<TResponse>(
            std::move(Participants),
            Options,
            [Object, Method, Tuple = std::make_tuple(std::forward<TArgs>(Args)...)]() mutable -> TResult<TResponse, FAppError>
            {
                return std::apply(
                    [Object, Method](auto&&... UnpackedArgs) -> TResult<TResponse, FAppError>
                    {
                        return (Object->*Method)(std::forward<decltype(UnpackedArgs)>(UnpackedArgs)...);
                    },
                    std::move(Tuple));
            });
    }

    uint64 SnapshotEpoch(uint64 PlayerId) const;
    uint64 EnsureEpoch(uint64 PlayerId);
    uint64 BumpEpoch(uint64 PlayerId);
    void RemovePlayer(uint64 PlayerId);

private:
    TOptional<FAppError> EnqueuePrepared(
        uint64 PlayerId,
        const SPlayerCommandOptions& Options,
        TPlayerCommandStart Start,
        TPlayerCommandFailure OnFailure);
    TOptional<FAppError> EnqueuePreparedForPlayers(
        TVector<SPlayerCommandParticipant> Participants,
        const SPlayerCommandOptions& Options,
        TPlayerCommandStart Start,
        TPlayerCommandFailure OnFailure);
    TOptional<FAppError> ResolveParticipantsForCommand(
        const TVector<SPlayerCommandParticipant>& Participants,
        const SPlayerCommandOptions& Options,
        TVector<TSharedPtr<MPlayerStrand>>& OutStrands,
        TVector<SPlayerCommandEpoch>& OutParticipantEpochs);
    TSharedPtr<MPlayerCommandState> BuildCommandState(
        const TVector<TSharedPtr<MPlayerStrand>>& CommandStrands);
    TSharedPtr<MFiberExecution> CreateCommandExecution(
        const TSharedPtr<MPlayerCommandState>& Command,
        const TVector<SPlayerCommandEpoch>& ParticipantEpochs,
        const SPlayerCommandOptions& Options,
        TPlayerCommandStart Start,
        TPlayerCommandFailure OnFailure);
    void StartCommandAcrossStrands(
        const TVector<TSharedPtr<MPlayerStrand>>& CommandStrands,
        const TSharedPtr<MFiberExecution>& Execution,
        TPlayerCommandFailure OnFailure);

    TSharedPtr<MPlayerStrand> GetOrCreateStrand(uint64 PlayerId, bool bCreateIfMissing);

    uint64 ResolveEpochForNewCommand(uint64 PlayerId, const SPlayerCommandOptions& Options);
    bool IsEpochValid(uint64 PlayerId, uint64 ExpectedEpoch) const;

private:
    ITaskRunner* Runner = nullptr;

    mutable std::mutex Mutex;
    TMap<uint64, TSharedPtr<MPlayerStrand>> Strands;
    TMap<uint64, uint64> PlayerEpochs;
};
