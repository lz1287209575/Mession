#pragma once

#include "Common/Runtime/Reflect/Reflection.h"

MSTRUCT()
struct FClientFindPlayerRequest
{
    MPROPERTY()
    uint64 PlayerId = 0;
};

MSTRUCT()
struct FClientFindPlayerResponse
{
    MPROPERTY()
    bool bFound = false;

    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint64 GatewayConnectionId = 0;

    MPROPERTY()
    uint32 SceneId = 0;

    MPROPERTY()
    MString Error;
};

MSTRUCT()
struct FClientQueryProfileRequest
{
    MPROPERTY()
    uint64 PlayerId = 0;
};

MSTRUCT()
struct FClientQueryProfileResponse
{
    MPROPERTY()
    bool bSuccess = false;

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

    MPROPERTY()
    MString Error;
};

MSTRUCT()
struct FClientQueryInventoryRequest
{
    MPROPERTY()
    uint64 PlayerId = 0;
};

MSTRUCT()
struct FClientQueryInventoryResponse
{
    MPROPERTY()
    bool bSuccess = false;

    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint32 Gold = 0;

    MPROPERTY()
    MString EquippedItem;

    MPROPERTY()
    MString Error;
};

MSTRUCT()
struct FClientQueryProgressionRequest
{
    MPROPERTY()
    uint64 PlayerId = 0;
};

MSTRUCT()
struct FClientQueryProgressionResponse
{
    MPROPERTY()
    bool bSuccess = false;

    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint32 Level = 0;

    MPROPERTY()
    uint32 Experience = 0;

    MPROPERTY()
    uint32 Health = 0;

    MPROPERTY()
    MString Error;
};

MSTRUCT()
struct FClientQueryPawnRequest
{
    MPROPERTY()
    uint64 PlayerId = 0;
};

MSTRUCT()
struct FClientQueryPawnResponse
{
    MPROPERTY()
    bool bSuccess = false;

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

    MPROPERTY()
    MString Error;
};
