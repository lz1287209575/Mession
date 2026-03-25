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
