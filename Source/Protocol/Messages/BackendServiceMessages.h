#pragma once

#include "Common/Runtime/Reflect/Reflection.h"

MSTRUCT()
struct FSceneEnterRequest
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint32 SceneId = 0;
};

MSTRUCT()
struct FSceneEnterResponse
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint32 SceneId = 0;
};

MSTRUCT()
struct FSceneLeaveRequest
{
    MPROPERTY()
    uint64 PlayerId = 0;
};

MSTRUCT()
struct FSceneLeaveResponse
{
    MPROPERTY()
    uint64 PlayerId = 0;
};

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

MSTRUCT()
struct FMgoLoadPlayerRequest
{
    MPROPERTY()
    uint64 PlayerId = 0;
};

MSTRUCT()
struct FMgoLoadPlayerResponse
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    MString Payload = "{}";
};

MSTRUCT()
struct FMgoSavePlayerRequest
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    MString Payload;
};

MSTRUCT()
struct FMgoSavePlayerResponse
{
    MPROPERTY()
    uint64 PlayerId = 0;
};
