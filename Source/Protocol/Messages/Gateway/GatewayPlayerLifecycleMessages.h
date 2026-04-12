#pragma once

#include "Common/Runtime/Reflect/Reflection.h"

MSTRUCT()
struct FClientLogoutRequest
{
    MPROPERTY()
    uint64 PlayerId = 0;
};

MSTRUCT()
struct FClientLogoutResponse
{
    MPROPERTY()
    bool bSuccess = false;

    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    MString Error;
};

MSTRUCT()
struct FClientSwitchSceneRequest
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint32 SceneId = 0;
};

MSTRUCT()
struct FClientSwitchSceneResponse
{
    MPROPERTY()
    bool bSuccess = false;

    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint32 SceneId = 0;

    MPROPERTY()
    MString Error;
};
