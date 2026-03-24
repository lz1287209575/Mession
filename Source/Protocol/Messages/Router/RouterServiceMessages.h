#pragma once

#include "Common/Runtime/Reflect/Reflection.h"

MSTRUCT()
struct FRouterResolvePlayerRouteRequest
{
    MPROPERTY()
    uint64 PlayerId = 0;
};

MSTRUCT()
struct FRouterResolvePlayerRouteResponse
{
    MPROPERTY()
    bool bFound = false;

    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint8 TargetServerType = 0;

    MPROPERTY()
    uint32 SceneId = 0;
};

MSTRUCT()
struct FRouterUpsertPlayerRouteRequest
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint8 TargetServerType = 0;

    MPROPERTY()
    uint32 SceneId = 0;
};

MSTRUCT()
struct FRouterUpsertPlayerRouteResponse
{
    MPROPERTY()
    uint64 PlayerId = 0;
};
