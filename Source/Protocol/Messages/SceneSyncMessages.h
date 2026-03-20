#pragma once

#include "Common/Runtime/Reflect/Reflection.h"

MSTRUCT()
struct SPlayerSceneStateMessage
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint16 SceneId = 0;

    MPROPERTY()
    float X = 0.0f;

    MPROPERTY()
    float Y = 0.0f;

    MPROPERTY()
    float Z = 0.0f;
};

MSTRUCT()
struct SPlayerSceneLeaveMessage
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint16 SceneId = 0;
};

MSTRUCT()
struct SGameplaySyncMessage
{
    MPROPERTY()
    uint64 ConnectionId = 0;

    MPROPERTY()
    TByteArray Data;
};

MSTRUCT()
struct SPlayerClientSyncMessage
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    TByteArray Data;
};

MSTRUCT()
struct SActorSpawnPayload
{
    MPROPERTY()
    uint64 ActorId = 0;

    MPROPERTY()
    TVector<uint8> Data;
};

MSTRUCT()
struct SActorReplicatePayload
{
    MPROPERTY()
    uint64 ActorId = 0;

    MPROPERTY()
    TVector<uint8> Data;
};

MSTRUCT()
struct SActorDespawnPayload
{
    MPROPERTY()
    uint64 ActorId = 0;
};

MSTRUCT()
struct SPlayerMovePayload
{
    MPROPERTY()
    float X = 0.0f;

    MPROPERTY()
    float Y = 0.0f;

    MPROPERTY()
    float Z = 0.0f;
};
