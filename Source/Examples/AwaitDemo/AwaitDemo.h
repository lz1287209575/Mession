// AwaitDemo — await 状态机框架端到端演示（C# async 模型，方案 B）
// 业务头 = 纯声明（像普通函数）；业务逻辑体（含 await 表达式）在 .cpp，
// 作为 codegen 输入（MHeaderTool 解析 .cpp 时收集 TAwaitable 站点）。

#pragma once

#include "Common/Runtime/Reflect/Reflection.h"
#include "Common/Runtime/Async/Awaitable.h"
#include "Common/Runtime/Async/MAsync.h"
#include "Common/Runtime/Concurrency/Promise.h"

// await 目标（模拟异步 I/O）：返回 ready 的 SFutureResult<int>（V*2）
SFutureResult<int> AwaitDemoHelper(int V);

// MFUNCTION(Async) 声明（纯声明——实现由 codegen 生成，业务逻辑体在 .cpp）
MFUNCTION(Async)
SFutureResult<int> AwaitDemoCompute(int Seed);
