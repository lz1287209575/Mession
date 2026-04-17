#pragma once

#include "Common/Runtime/Reflect/Reflection.h"
#include "Protocol/Messages/World/PlayerQueryMessages.h"
#include "Protocol/Messages/World/PlayerRouteMessages.h"

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

MSTRUCT()
struct FClientQueryCombatProfileResponse
{
    MPROPERTY()
    bool bSuccess = false;

    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint32 BaseAttack = 0;

    MPROPERTY()
    uint32 BaseDefense = 0;

    MPROPERTY()
    uint32 MaxHealth = 0;

    MPROPERTY()
    uint32 PrimarySkillId = 0;

    MPROPERTY()
    uint32 LastResolvedSceneId = 0;

    MPROPERTY()
    uint32 LastResolvedHealth = 0;

    MPROPERTY()
    MString Error;
};
