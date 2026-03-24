#pragma once

#include "Common/Runtime/Reflect/Reflection.h"

MSTRUCT()
struct FClientEchoRequest
{
    MPROPERTY()
    MString Message;
};

MSTRUCT()
struct FClientEchoResponse
{
    MPROPERTY()
    uint64 ConnectionId = 0;

    MPROPERTY()
    MString Message;
};

MSTRUCT()
struct FClientLoginRequest
{
    MPROPERTY()
    uint64 PlayerId = 0;
};

MSTRUCT()
struct FClientLoginResponse
{
    MPROPERTY()
    bool bSuccess = false;

    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint32 SessionKey = 0;

    MPROPERTY()
    MString Error;
};

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
