#pragma once

#include "Common/Runtime/Reflect/Reflection.h"
#include "Servers/App/ServerCallRequestValidation.h"

MSTRUCT()
struct FPlayerMoveRequest
{
    MPROPERTY(Meta=(NonZero, ErrorCode="player_id_required", ErrorContext="PlayerMove"))
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
