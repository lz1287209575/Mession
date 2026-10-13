/**
 * @file ControlPlaneMessages.h
 * @brief 控制平面协议的残留占位类型。
 *
 * 原 ControlPlane 协议(节点注册 / 路由 / 心跳)已在 v2 重构中删除,本文件只剩一个
 * 空响应占位。若将来要重新引入控制平面消息,应另建独立 .h 文件,不要往这里堆。
 */
#pragma once

#include "Common/Runtime/Reflect/Reflection.h"

/**
 * @brief SEmptyServerMessage - 空 ServerCall 响应占位。
 *
 * 给"没有返回值、但仍需满足 ServerCall 响应形状"的入口用(如 PushClientDownlink)。
 * 本类型没有任何 MPROPERTY 字段,序列化后是零长度 payload —— 这是有意为之,
 * 不要为了"看起来完整"往里加字段(加字段会改变该入口的协议形状)。
 */
MSTRUCT()
struct SEmptyServerMessage {};
