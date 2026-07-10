#pragma once

#include "Common/Runtime/Object/Object.h"

/**
 * MClientDownlink — UE 客户端下行反射占位类。
 *
 * RpcClientCall 通过 `MClientDownlink::StaticClass()` 拿到 MClass，
 * 用 MGET_STABLE_RPC_FUNCTION_ID 算 Client_* FunctionId 来分发下行包。
 *
 * 所有 MFUNCTION 是空实现——业务侧通常继承 MClientDownlink 加业务 Client_* 方法，
 * 然后通过 `RPC_DISPATCH_TO(MyDownlink, Client_OnXxx, connId, payload)` 走泛型分发。
 *
 * 当前 Gateway 流程走的是 MClientManifest（由 MHeaderTool 自动生成），
 * 不再依赖这个反射表，所以这里仅保留 Class 元信息 + 最小占位。
 */
MCLASS(Type=Object)
class MClientDownlink : public MObject
{
public:
    MGENERATED_BODY(MClientDownlink, MObject, 0)
};