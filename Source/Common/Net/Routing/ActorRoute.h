#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Net/ServerConnection.h"

struct SActorRoute
{
    uint64 ActorId = 0;
    EServerType ServerType = EServerType::Unknown;
    uint64 ConnectionId = 0;
    uint64 LastUpdateTime = 0;
};

struct FLocateActorResult
{
    bool bFound = false;
    EServerType ServerType = EServerType::Unknown;
    uint64 ConnectionId = 0;
    MString TargetClassName;
    MString MethodName;
};
