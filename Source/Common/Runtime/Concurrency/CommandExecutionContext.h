#pragma once

#include "Common/Runtime/Concurrency/ITaskRunner.h"
#include "Common/Runtime/MLib.h"

class MPlayerCommandRuntime;

struct SPlayerCommandEpoch
{
    uint64 PlayerId = 0;
    uint64 Epoch = 0;
};

class MPlayerCommandContext
{
public:
    MPlayerCommandContext() = default;

    MPlayerCommandContext(
        TVector<SPlayerCommandEpoch> InParticipants,
        const char* InCommandName,
        ITaskRunner* InRunner,
        MPlayerCommandRuntime* InRuntime = nullptr)
        : Participants(std::move(InParticipants))
        , CommandName(InCommandName ? InCommandName : "")
        , Runner(InRunner)
        , Runtime(InRuntime)
    {
    }

    uint64 GetPlayerId() const
    {
        return Participants.empty() ? 0 : Participants.front().PlayerId;
    }

    uint64 GetEpoch() const
    {
        return Participants.empty() ? 0 : Participants.front().Epoch;
    }

    const TVector<SPlayerCommandEpoch>& GetParticipants() const
    {
        return Participants;
    }

    const char* GetCommandName() const
    {
        return CommandName.c_str();
    }

    ITaskRunner* GetRunner() const
    {
        return Runner;
    }

    MPlayerCommandRuntime* GetRuntime() const
    {
        return Runtime;
    }

private:
    TVector<SPlayerCommandEpoch> Participants;
    MString CommandName;
    ITaskRunner* Runner = nullptr;
    MPlayerCommandRuntime* Runtime = nullptr;
};
