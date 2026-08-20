#pragma once

// FSceneMessages.h — 场景域真实形状消息（#5 协议样例：嵌套结构/数组/浮点坐标）。

#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Reflect/Reflection.h"

// 三维向量（坐标/旋转共用）
MSTRUCT()
struct FVector3 {
    MPROPERTY()
    float X = 0.0f;

    MPROPERTY()
    float Y = 0.0f;

    MPROPERTY()
    float Z = 0.0f;
};

// 场景内 Actor 生成信息
MSTRUCT()
struct FActorSpawnInfo {
    MPROPERTY()
    uint64 ActorId = 0;

    MPROPERTY()
    MString Prefab;

    MPROPERTY()
    FVector3 Position;
};

// 玩家进场景消息（含生成列表）
MSTRUCT()
struct FSceneEnterMsg {
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    MString SceneId;

    MPROPERTY()
    FVector3 Position;

    MPROPERTY()
    FVector3 Rotation;

    MPROPERTY()
    TVector<FActorSpawnInfo> Actors;
};
