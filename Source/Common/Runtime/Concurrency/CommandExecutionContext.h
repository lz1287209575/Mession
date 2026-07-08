#pragma once

#include "Common/Runtime/Concurrency/ITaskRunner.h"
#include "Common/Runtime/MLib.h"

// MPlayerCommandRuntime 是 World 进程内的 Player 命令调度器；架构 v2 不再依赖 World，
// 这里只保留前向声明 + 一个空的虚拟基类，供旧 fiber 工具链继续编译通过。
class MPlayerCommandRuntime
{
public:
    virtual ~MPlayerCommandRuntime() = default;
    virtual uint64 SnapshotEpoch(uint64 PlayerId) const { (void)PlayerId; return 0; }
};

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
