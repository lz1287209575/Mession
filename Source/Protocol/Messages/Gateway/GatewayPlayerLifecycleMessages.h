#pragma once

#include "Common/Runtime/Reflect/Reflection.h"
#include "Protocol/Messages/World/PlayerLifecycleMessages.h"

MSTRUCT()
struct FClientLogoutResponse
{
    MPROPERTY()
    bool bSuccess = false;

    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    MString Error;
};

MSTRUCT()
struct FClientSwitchSceneResponse
{
    MPROPERTY()
    bool bSuccess = false;

    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint32 SceneId = 0;

    MPROPERTY()
    MString Error;
};
