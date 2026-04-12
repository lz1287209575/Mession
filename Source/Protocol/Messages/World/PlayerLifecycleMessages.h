#pragma once

#include "Common/Runtime/Reflect/Reflection.h"

MSTRUCT()
struct FPlayerEnterWorldRequest
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint64 GatewayConnectionId = 0;

    MPROPERTY()
    uint32 SessionKey = 0;
};

MSTRUCT()
struct FPlayerEnterWorldResponse
{
    MPROPERTY()
    uint64 PlayerId = 0;
};

MSTRUCT()
struct FPlayerLogoutRequest
{
    MPROPERTY()
    uint64 PlayerId = 0;
};

MSTRUCT()
struct FPlayerLogoutResponse
{
    MPROPERTY()
    uint64 PlayerId = 0;
};

MSTRUCT()
struct FPlayerSwitchSceneRequest
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint32 SceneId = 0;
};

MSTRUCT()
struct FPlayerSwitchSceneResponse
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint32 SceneId = 0;
};
