#pragma once

#include "Common/Runtime/Reflect/Reflection.h"

#include <utility>

// Client dispatch 反射接口 —— step-2 后只剩 `DispatchClientFunction` 一条路径。
// 原 EClientMessageType / SClientRouteRequest / IClientRouteTarget / IClientResponseTarget
// 整套都已被 step-2 拆掉(它们的语义由 Gateway 端 Manifest 反射 + ServerCall
// ServerCallAsyncSupport 取代)。
//
// 这一步不再做"按消息名 dispatch",Gateway 收到 envelope 后:
//   1. 读 14 字节头 [RequestId:8B][FunctionId:2B][Size:4B]
//   2. 反射查 MClientManifest::FindByFunctionId 拿到 (OwnerType, MethodName, RequestType)
//   3. 反射 ParsePayload(RequestType) 解业务参数
//   4. 调 MRpcChannel::Call<RespType>(...) 走 server↔server RPC 路径
// 反射入口只暴露 FunctionId。

const MFunction* FindClientFunctionById(const MClass* TargetClass, uint16 FunctionId);