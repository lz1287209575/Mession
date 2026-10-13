/**
 * @file FSceneMessages.h
 * @brief 场景域协议消息 - 嵌套结构 / 数组 / 浮点坐标的真实形状样例。
 *
 * 与战斗域消息一起作为反射序列化的 round-trip 验证输入(见
 * Protocol/Tests/ProtocolMessagesTest.cpp)。
 *
 * @note 字段构成 MPROPERTY 序列化形状(对外协议 ABI),增删 / 重排必须与客户端同步。
 */
#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Reflect/Reflection.h"

/**
 * @brief FVector3 - 三维向量,坐标与旋转共用。
 */
MSTRUCT()
struct FVector3 {
    /// X 分量
    MPROPERTY()
    float X = 0.0f;

    /// Y 分量;旋转场景下即 yaw
    MPROPERTY()
    float Y = 0.0f;

    /// Z 分量
    MPROPERTY()
    float Z = 0.0f;
};

/**
 * @brief FActorSpawnInfo - 场景内一个 Actor 的生成信息。
 */
MSTRUCT()
struct FActorSpawnInfo {
    /// Actor 全局 id
    MPROPERTY()
    uint64 ActorId = 0;

    /// 预制体资源名(由资产系统解析)
    MPROPERTY()
    MString Prefab;

    /// 生成位置(世界坐标)
    MPROPERTY()
    FVector3 Position;
};

/**
 * @brief FSceneEnterMsg - 玩家进入场景消息(含随场景下发的生成列表)。
 */
MSTRUCT()
struct FSceneEnterMsg {
    /// 玩家 id
    MPROPERTY()
    uint64 PlayerId = 0;

    /// 场景 id
    MPROPERTY()
    MString SceneId;

    /// 玩家出生位置
    MPROPERTY()
    FVector3 Position;

    /// 玩家朝向
    MPROPERTY()
    FVector3 Rotation;

    /// 需要为玩家生成的 Actor 列表,可为空
    MPROPERTY()
    TVector<FActorSpawnInfo> Actors;
};
