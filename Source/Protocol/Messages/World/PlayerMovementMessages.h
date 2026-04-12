#pragma once

#include "Common/Runtime/Reflect/Reflection.h"

MSTRUCT()
struct FPlayerMoveRequest
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    float X = 0.0f;

    MPROPERTY()
    float Y = 0.0f;

    MPROPERTY()
    float Z = 0.0f;
};

MSTRUCT()
struct FPlayerMoveResponse
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint32 SceneId = 0;

    MPROPERTY()
    float X = 0.0f;

    MPROPERTY()
    float Y = 0.0f;

    MPROPERTY()
    float Z = 0.0f;

    MPROPERTY()
    uint32 Health = 0;
};
