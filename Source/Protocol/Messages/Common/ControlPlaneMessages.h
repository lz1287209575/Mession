#pragma once

#include "Common/Runtime/Reflect/Reflection.h"

/**
 * SEmptyServerMessage — 空 ServerCall 响应占位（PushClientDownlink 等返回类型）。
 *
 * ControlPlane 协议（节点注册 / 路由 / 心跳）已在 v2 重构中删除。
 * 如果未来需要重新引入，再加独立 .h 文件。
 */
MSTRUCT()
struct SEmptyServerMessage {};
