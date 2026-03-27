#pragma once

#include "Common/Runtime/Reflect/Reflection.h"

MSTRUCT()
struct FPlayerEnterWorldRequest
{
    MPROPERTY()
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
struct FPlayerFindRequest
{
    MPROPERTY()
    uint64 PlayerId = 0;
};

MSTRUCT()
struct FPlayerFindResponse
{
    MPROPERTY()
    bool bFound = false;

    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint64 GatewayConnectionId = 0;

    MPROPERTY()
    uint32 SceneId = 0;
};

MSTRUCT()
struct FPlayerUpdateRouteRequest
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint8 TargetServerType = 0;

    MPROPERTY()
    uint32 SceneId = 0;
};

MSTRUCT()
struct FPlayerUpdateRouteResponse
{
    MPROPERTY()
    uint64 PlayerId = 0;
};

MSTRUCT()
struct FPlayerLogoutRequest
{
    MPROPERTY()
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
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
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

MSTRUCT()
struct FPlayerApplyRouteRequest
{
    MPROPERTY()
    uint32 SceneId = 0;

    MPROPERTY()
    uint8 TargetServerType = 0;
};

MSTRUCT()
struct FPlayerApplyRouteResponse
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint32 SceneId = 0;

    MPROPERTY()
    uint8 TargetServerType = 0;
};

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
    MPROPERTY()
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
struct FPlayerQueryInventoryRequest
{
    MPROPERTY()
    uint64 PlayerId = 0;
};

MSTRUCT()
struct FPlayerQueryInventoryResponse
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint32 Gold = 0;

    MPROPERTY()
    MString EquippedItem;
};

MSTRUCT()
struct FPlayerQueryProgressionRequest
{
    MPROPERTY()
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
struct FPlayerChangeGoldRequest
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    int32 DeltaGold = 0;
};

MSTRUCT()
struct FPlayerChangeGoldResponse
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint32 Gold = 0;
};

MSTRUCT()
struct FPlayerEquipItemRequest
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    MString EquippedItem;
};

MSTRUCT()
struct FPlayerEquipItemResponse
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    MString EquippedItem;
};

MSTRUCT()
struct FPlayerGrantExperienceRequest
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint32 ExperienceDelta = 0;
};

MSTRUCT()
struct FPlayerGrantExperienceResponse
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint32 Level = 0;

    MPROPERTY()
    uint32 Experience = 0;
};

MSTRUCT()
struct FPlayerModifyHealthRequest
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    int32 HealthDelta = 0;
};

MSTRUCT()
struct FPlayerModifyHealthResponse
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint32 Health = 0;
};
