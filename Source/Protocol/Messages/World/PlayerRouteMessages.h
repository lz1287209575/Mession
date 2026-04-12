#pragma once

#include "Common/Runtime/Reflect/Reflection.h"

MSTRUCT()
struct FPlayerFindRequest
{
    MPROPERTY()
    uint64 PlayerId = 0;
};

MSTRUCT()
struct FPlayerFindResponse
{
    MPROPERTY()
    bool bFound = false;

    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint64 GatewayConnectionId = 0;

    MPROPERTY()
    uint32 SceneId = 0;
};

MSTRUCT()
struct FPlayerUpdateRouteRequest
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint8 TargetServerType = 0;

    MPROPERTY()
    uint32 SceneId = 0;
};

MSTRUCT()
struct FPlayerUpdateRouteResponse
{
    MPROPERTY()
    uint64 PlayerId = 0;
};

MSTRUCT()
struct FPlayerApplyRouteRequest
{
    MPROPERTY()
    uint32 SceneId = 0;

    MPROPERTY()
    uint8 TargetServerType = 0;
};

MSTRUCT()
struct FPlayerApplyRouteResponse
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint32 SceneId = 0;

    MPROPERTY()
    uint8 TargetServerType = 0;
};
