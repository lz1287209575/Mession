#pragma once

#include "Common/Runtime/Reflect/Reflection.h"
#include "Servers/App/ServerCallRequestValidation.h"

MSTRUCT()
struct FPlayerEnterWorldRequest
{
    MPROPERTY(Meta=(NonZero, ErrorCode="player_id_required", ErrorContext="PlayerEnterWorld"))
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
    MPROPERTY(Meta=(NonZero, ErrorCode="player_id_required", ErrorContext="PlayerLogout"))
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
    MPROPERTY(Meta=(NonZero, ErrorCode="player_id_required", ErrorContext="PlayerSwitchScene"))
    uint64 PlayerId = 0;

    MPROPERTY(Meta=(NonZero, ErrorCode="scene_id_required", ErrorContext="PlayerSwitchScene"))
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
