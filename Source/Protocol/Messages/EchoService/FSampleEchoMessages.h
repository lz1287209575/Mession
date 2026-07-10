#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Reflect/Reflection.h"

MSTRUCT()
struct FSampleEchoRequest
{
    MPROPERTY()
    MString Message;

    // 目标 ActorId（64-bit 位布局：[ServiceId: high 32][InstId: low 32]）。
    // 客户端传过来时是 raw uint64；本进程用 MServiceId::Make/Get* 拆解。
    MPROPERTY(Meta=(NonZero, ErrorCode="actor_id_required", ErrorContext="SampleEcho"))
    uint64 TargetActorId = 0;
};

MSTRUCT()
struct FSampleEchoResponse
{
    MPROPERTY()
    MString Echo;

    // 主叫 ActorId（同样 64-bit 位布局）。远端收到后能用 MServiceId::GetServiceType()
    // 知道主叫来自哪种 Service，再通过 MActorRouter::FindActor 寻址反调。
    MPROPERTY()
    uint64 SourceActorId = 0;

    MPROPERTY()
    MString SourceServerName;
};
