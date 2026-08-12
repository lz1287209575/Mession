// ComplexDemo — 复杂 await 演示（方案 B：头纯声明，体在 .cpp）
// 1) ComplexChain: 多 await 串行 + 中间业务逻辑 + 跨 await 存活变量
// 2) ComplexLoop:  循环 await + 累加

#pragma once

#include "Common/Runtime/Reflect/Reflection.h"
#include "Common/Runtime/Async/Awaitable.h"
#include "Common/Runtime/Async/MAsync.h"
#include "Common/Runtime/Concurrency/Promise.h"

// await 目标（同步）：返回 ready 的 SFutureResult<int>（A + B）
SFutureResult<int> AsyncAdd(int A, int B);

// await 目标（真正异步）：延迟 50ms 完成（模拟异步 I/O）——触发 suspend/恢复路径
SFutureResult<int> AsyncDelayed(int V);

// 多 await 串行（2 个 await + 中间业务逻辑）
MFUNCTION(Async)
SFutureResult<int> ComplexChain(int Base);

// 循环 await + 累加
MFUNCTION(Async)
SFutureResult<int> ComplexLoop(int N);

// await 真正异步的目标（挂起 → 完成回调 → 恢复）
MFUNCTION(Async)
SFutureResult<int> ComplexAsync(int Base);
