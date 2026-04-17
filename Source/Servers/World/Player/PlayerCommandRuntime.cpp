#include "Servers/World/Player/PlayerCommandRuntime.h"

#include "Common/Runtime/Concurrency/FiberAwait.h"
#include "Common/Runtime/Concurrency/FiberScheduler.h"

namespace
{
TVector<SPlayerCommandParticipant> NormalizeParticipants(TVector<SPlayerCommandParticipant> Participants)
{
    TVector<SPlayerCommandParticipant> Result;
    Result.reserve(Participants.size());
    for (const SPlayerCommandParticipant& Participant : Participants)
    {
        bool bExists = false;
        for (const SPlayerCommandParticipant& Existing : Result)
        {
            if (Existing.PlayerId == Participant.PlayerId)
            {
                bExists = true;
                break;
            }
        }

        if (!bExists)
        {
            Result.push_back(Participant);
        }
    }

    return Result;
}

FAppError MakePlayerCommandError(const char* Code, const char* CommandName, const char* Message = "")
{
    return FAppError::Make(
        Code ? Code : "player_command_failed",
        CommandName ? CommandName : (Message ? Message : ""));
}
}

class MPlayerStrand : public TEnableSharedFromThis<MPlayerStrand>
{
public:
    explicit MPlayerStrand(ITaskRunner* InRunner)
        : Runner(InRunner)
    {
    }

    void EnqueueCommand(ITaskRunner::TTask Task)
    {
        bool bShouldSchedule = false;
        {
            std::lock_guard<std::mutex> Lock(Mutex);
            PendingCommands.push_back(std::move(Task));
            if (!bCommandActive && !bPumpPosted)
            {
                bPumpPosted = true;
                bShouldSchedule = true;
            }
        }

        if (bShouldSchedule && Runner)
        {
            Runner->PostTask([Self = this->shared_from_this()]()
            {
                Self->PumpNextCommand();
            });
        }
    }

    void PostContinuation(ITaskRunner::TTask Task)
    {
        if (!Runner || !Task)
        {
            return;
        }

        // Continuation belongs to the current active command; do not advance the queue.
        Runner->PostTask(std::move(Task));
    }

    void OnCommandCompleted()
    {
        bool bShouldSchedule = false;
        {
            std::lock_guard<std::mutex> Lock(Mutex);
            bCommandActive = false;
            if (!PendingCommands.empty() && !bPumpPosted)
            {
                bPumpPosted = true;
                bShouldSchedule = true;
            }
        }

        if (bShouldSchedule && Runner)
        {
            Runner->PostTask([Self = this->shared_from_this()]()
            {
                Self->PumpNextCommand();
            });
        }
    }

private:
    void PumpNextCommand()
    {
        ITaskRunner::TTask Task;
        {
            std::lock_guard<std::mutex> Lock(Mutex);
            bPumpPosted = false;
            if (bCommandActive || PendingCommands.empty())
            {
                return;
            }

            bCommandActive = true;
            Task = std::move(PendingCommands.front());
            PendingCommands.pop_front();
        }

        if (Task)
        {
            Task();
        }
    }

private:
    ITaskRunner* Runner = nullptr;
    TDeque<ITaskRunner::TTask> PendingCommands;
    bool bCommandActive = false;
    bool bPumpPosted = false;
    std::mutex Mutex;
};

class MPlayerCommandState
{
public:
    TVector<TSharedPtr<MPlayerStrand>> Strands;
};

namespace
{
void CompleteCommandStrands(const TSharedPtr<MPlayerCommandState>& Command)
{
    if (!Command)
    {
        return;
    }

    for (const TSharedPtr<MPlayerStrand>& Strand : Command->Strands)
    {
        if (Strand)
        {
            Strand->OnCommandCompleted();
        }
    }
}
}

TOptional<FAppError> MPlayerCommandRuntime::EnqueuePrepared(
    uint64 PlayerId,
    const SPlayerCommandOptions& Options,
    TPlayerCommandStart Start,
    TPlayerCommandFailure OnFailure)
{
    TVector<SPlayerCommandParticipant> Participants;
    Participants.push_back(SPlayerCommandParticipant{
        PlayerId,
        Options.ExpectedEpoch,
        Options.bCreateStrandIfMissing,
    });
    return EnqueuePreparedForPlayers(
        std::move(Participants),
        Options,
        std::move(Start),
        std::move(OnFailure));
}

TOptional<FAppError> MPlayerCommandRuntime::EnqueuePreparedForPlayers(
    TVector<SPlayerCommandParticipant> Participants,
    const SPlayerCommandOptions& Options,
    TPlayerCommandStart Start,
    TPlayerCommandFailure OnFailure)
{
    Participants = NormalizeParticipants(std::move(Participants));
    if (Participants.empty())
    {
        return MakePlayerCommandError("player_id_required", Options.CommandName);
    }

    for (const SPlayerCommandParticipant& Participant : Participants)
    {
        if (Participant.PlayerId == 0)
        {
            return MakePlayerCommandError("player_id_required", Options.CommandName);
        }
    }

    if (!Runner)
    {
        return MakePlayerCommandError("player_command_runner_missing", Options.CommandName);
    }

    TVector<TSharedPtr<MPlayerStrand>> CommandStrands;
    TVector<SPlayerCommandEpoch> ParticipantEpochs;
    if (const TOptional<FAppError> Error = ResolveParticipantsForCommand(
            Participants,
            Options,
            CommandStrands,
            ParticipantEpochs);
        Error.has_value())
    {
        return Error;
    }

    TSharedPtr<MPlayerCommandState> Command = BuildCommandState(CommandStrands);
    TPlayerCommandFailure FailureCallback = std::move(OnFailure);
    TSharedPtr<MFiberExecution> Execution = CreateCommandExecution(
        Command,
        ParticipantEpochs,
        Options,
        std::move(Start),
        FailureCallback);

    if (!Execution)
    {
        return MakePlayerCommandError("player_command_create_failed", Options.CommandName);
    }

    StartCommandAcrossStrands(CommandStrands, Execution, std::move(FailureCallback));

    return std::nullopt;
}

TOptional<FAppError> MPlayerCommandRuntime::ResolveParticipantsForCommand(
    const TVector<SPlayerCommandParticipant>& Participants,
    const SPlayerCommandOptions& Options,
    TVector<TSharedPtr<MPlayerStrand>>& OutStrands,
    TVector<SPlayerCommandEpoch>& OutParticipantEpochs)
{
    OutStrands.clear();
    OutParticipantEpochs.clear();
    OutStrands.reserve(Participants.size());
    OutParticipantEpochs.reserve(Participants.size());

    for (const SPlayerCommandParticipant& Participant : Participants)
    {
        if (Participant.ExpectedEpoch != 0 && !IsEpochValid(Participant.PlayerId, Participant.ExpectedEpoch))
        {
            return MakePlayerCommandError("player_command_stale", Options.CommandName);
        }

        TSharedPtr<MPlayerStrand> Strand = GetOrCreateStrand(
            Participant.PlayerId,
            Participant.bCreateStrandIfMissing);
        if (!Strand)
        {
            return MakePlayerCommandError("player_command_strand_missing", Options.CommandName);
        }

        OutStrands.push_back(Strand);
        OutParticipantEpochs.push_back(SPlayerCommandEpoch{
            Participant.PlayerId,
            ResolveEpochForNewCommand(Participant.PlayerId, SPlayerCommandOptions{
                Options.CommandName,
                Participant.ExpectedEpoch,
                Participant.bCreateStrandIfMissing,
            }),
        });
    }

    return std::nullopt;
}

TSharedPtr<MPlayerCommandState> MPlayerCommandRuntime::BuildCommandState(
    const TVector<TSharedPtr<MPlayerStrand>>& CommandStrands)
{
    TSharedPtr<MPlayerCommandState> Command = MakeShared<MPlayerCommandState>();
    Command->Strands = CommandStrands;
    return Command;
}

TSharedPtr<MFiberExecution> MPlayerCommandRuntime::CreateCommandExecution(
    const TSharedPtr<MPlayerCommandState>& Command,
    const TVector<SPlayerCommandEpoch>& ParticipantEpochs,
    const SPlayerCommandOptions& Options,
    TPlayerCommandStart Start,
    TPlayerCommandFailure OnFailure)
{
    MPlayerCommandContext Context(
        ParticipantEpochs,
        Options.CommandName,
        Runner,
        this);
    TPlayerCommandFailure FailureCallback = std::move(OnFailure);

    return MFiberScheduler::Get().CreateExecution(
        {
            Command,
            std::move(Context),
            [this, ParticipantEpochs, Start = std::move(Start)]() mutable
            {
                for (const SPlayerCommandEpoch& Participant : ParticipantEpochs)
                {
                    if (SnapshotEpoch(Participant.PlayerId) != Participant.Epoch)
                    {
                        throw std::runtime_error("player command became stale before execution");
                    }
                }

                Start();
            },
            [Command]()
            {
                CompleteCommandStrands(Command);
            },
            [Command, FailureCallback](std::exception_ptr Exception) mutable
            {
                FAppError Error = FAppError::Make("player_command_exception", "unknown");

                try
                {
                    if (Exception)
                    {
                        std::rethrow_exception(Exception);
                    }
                }
                catch (const MPlayerCommandDetail::FPlayerCommandError& Ex)
                {
                    Error = Ex.GetError();
                }
                catch (const MPlayerCommandDetail::FPlayerCommandAbort& Ex)
                {
                    Error = FAppError::Make("player_command_stale", Ex.what());
                }
                catch (const std::exception& Ex)
                {
                    Error = FAppError::Make("player_command_exception", Ex.what());
                }
                catch (...)
                {
                    Error = FAppError::Make("player_command_exception", "unknown");
                }

                if (FailureCallback)
                {
                    FailureCallback(std::move(Error));
                }

                CompleteCommandStrands(Command);
            },
        });
}

void MPlayerCommandRuntime::StartCommandAcrossStrands(
    const TVector<TSharedPtr<MPlayerStrand>>& CommandStrands,
    const TSharedPtr<MFiberExecution>& Execution,
    TPlayerCommandFailure OnFailure)
{
    struct FStartBarrierState
    {
        std::mutex Mutex;
        size_t PendingStrands = 0;
        bool bStarted = false;
    };

    TSharedPtr<FStartBarrierState> Barrier = MakeShared<FStartBarrierState>();
    Barrier->PendingStrands = CommandStrands.size();

    auto StartExecution =
        [Execution, FailureCallback = std::move(OnFailure)]() mutable
        {
            try
            {
                MFiberScheduler::Get().Start(Execution);
            }
            catch (const std::exception& Ex)
            {
                if (FailureCallback)
                {
                    FailureCallback(FAppError::Make("player_command_start_failed", Ex.what()));
                }

                CompleteCommandStrands(Execution->GetCommandState());
            }
            catch (...)
            {
                if (FailureCallback)
                {
                    FailureCallback(FAppError::Make("player_command_start_failed", "unknown"));
                }

                CompleteCommandStrands(Execution->GetCommandState());
            }
        };

    for (const TSharedPtr<MPlayerStrand>& Strand : CommandStrands)
    {
        Strand->EnqueueCommand(
            [Barrier, StartExecution]() mutable
            {
                bool bShouldStart = false;
                {
                    std::lock_guard<std::mutex> Lock(Barrier->Mutex);
                    if (Barrier->PendingStrands > 0)
                    {
                        --Barrier->PendingStrands;
                    }

                    if (Barrier->PendingStrands == 0 && !Barrier->bStarted)
                    {
                        Barrier->bStarted = true;
                        bShouldStart = true;
                    }
                }

                if (bShouldStart)
                {
                    StartExecution();
                }
            });
    }
}

TSharedPtr<MPlayerStrand> MPlayerCommandRuntime::GetOrCreateStrand(uint64 PlayerId, bool bCreateIfMissing)
{
    std::lock_guard<std::mutex> Lock(Mutex);

    const auto Existing = Strands.find(PlayerId);
    if (Existing != Strands.end())
    {
        return Existing->second;
    }

    if (!bCreateIfMissing)
    {
        return nullptr;
    }

    TSharedPtr<MPlayerStrand> Strand = MakeShared<MPlayerStrand>(Runner);
    Strands.emplace(PlayerId, Strand);
    return Strand;
}

uint64 MPlayerCommandRuntime::ResolveEpochForNewCommand(uint64 PlayerId, const SPlayerCommandOptions& Options)
{
    if (Options.ExpectedEpoch != 0)
    {
        return Options.ExpectedEpoch;
    }

    return EnsureEpoch(PlayerId);
}

bool MPlayerCommandRuntime::IsEpochValid(uint64 PlayerId, uint64 ExpectedEpoch) const
{
    std::lock_guard<std::mutex> Lock(Mutex);

    const auto Existing = PlayerEpochs.find(PlayerId);
    if (Existing == PlayerEpochs.end())
    {
        return false;
    }

    return Existing->second == ExpectedEpoch;
}

uint64 MPlayerCommandRuntime::SnapshotEpoch(uint64 PlayerId) const
{
    std::lock_guard<std::mutex> Lock(Mutex);

    const auto Existing = PlayerEpochs.find(PlayerId);
    return Existing != PlayerEpochs.end() ? Existing->second : 0;
}

uint64 MPlayerCommandRuntime::EnsureEpoch(uint64 PlayerId)
{
    std::lock_guard<std::mutex> Lock(Mutex);

    uint64& Epoch = PlayerEpochs[PlayerId];
    if (Epoch == 0)
    {
        Epoch = 1;
    }

    return Epoch;
}

uint64 MPlayerCommandRuntime::BumpEpoch(uint64 PlayerId)
{
    std::lock_guard<std::mutex> Lock(Mutex);

    uint64& Epoch = PlayerEpochs[PlayerId];
    if (Epoch == 0)
    {
        Epoch = 1;
    }

    ++Epoch;
    return Epoch;
}

void MPlayerCommandRuntime::RemovePlayer(uint64 PlayerId)
{
    std::lock_guard<std::mutex> Lock(Mutex);
    Strands.erase(PlayerId);
    PlayerEpochs.erase(PlayerId);
}
