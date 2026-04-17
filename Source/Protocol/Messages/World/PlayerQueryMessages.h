#pragma once

#include "Common/Runtime/Reflect/Reflection.h"
#include "Servers/App/ServerCallRequestValidation.h"

MSTRUCT()
struct FPlayerQueryStateRequest
{
};

MSTRUCT()
struct FPlayerQueryStateResponse
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint64 GatewayConnectionId = 0;

    MPROPERTY()
    uint32 SceneId = 0;
};

MSTRUCT()
struct FPlayerQueryProfileRequest
{
    MPROPERTY(Meta=(NonZero, ErrorCode="player_id_required", ErrorContext="PlayerQueryProfile"))
    uint64 PlayerId = 0;
};

MSTRUCT()
struct FPlayerQueryProfileResponse
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint32 CurrentSceneId = 0;

    MPROPERTY()
    uint32 Gold = 0;

    MPROPERTY()
    MString EquippedItem;

    MPROPERTY()
    uint32 Level = 0;

    MPROPERTY()
    uint32 Experience = 0;

    MPROPERTY()
    uint32 Health = 0;
};

MSTRUCT()
struct FPlayerQueryProgressionRequest
{
    MPROPERTY(Meta=(NonZero, ErrorCode="player_id_required", ErrorContext="PlayerQueryProgression"))
    uint64 PlayerId = 0;
};

MSTRUCT()
struct FPlayerQueryProgressionResponse
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint32 Level = 0;

    MPROPERTY()
    uint32 Experience = 0;

    MPROPERTY()
    uint32 Health = 0;
};

MSTRUCT()
struct FPlayerQueryPawnRequest
{
    MPROPERTY(Meta=(NonZero, ErrorCode="player_id_required", ErrorContext="PlayerQueryPawn"))
    uint64 PlayerId = 0;
};

MSTRUCT()
struct FPlayerQueryPawnResponse
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
