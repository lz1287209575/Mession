/**
 * @file ComplexDemo.h
 * @brief ComplexDemo - 复杂 await 场景的声明(方案 B:头纯声明,体在 .cpp)。
 *
 * 1. ComplexChain: 多 await 串行 + 中间业务逻辑 + 跨 await 存活的变量;
 * 2. ComplexLoop:  循环内 await + 累加。
 */

#pragma once

#include "Common/Runtime/Async/Awaitable.h"
#include "Common/Runtime/Async/MAsync.h"
#include "Common/Runtime/Concurrency/Promise.h"
#include "Common/Runtime/Reflect/Reflection.h"

/**
 * @brief AsyncAdd - await 目标(同步):返回已就绪的 A + B。
 */
SFutureResult<int> AsyncAdd(int A, int B);

/**
 * @brief AsyncDelayed - await 目标(真异步):延迟 50ms 完成,用来触发 suspend / resume 路径。
 */
SFutureResult<int> AsyncDelayed(int V);

/** @brief ComplexChain - 多 await 串行(两个 await + 中间业务逻辑)。 */
MFUNCTION(Async)
SFutureResult<int> ComplexChain(int Base);

/** @brief ComplexLoop - 循环内 await 并累加。 */
MFUNCTION(Async)
SFutureResult<int> ComplexLoop(int N);

/** @brief ComplexAsync - await 真异步目标:挂起 → 完成回调 → 恢复。 */
MFUNCTION(Async)
SFutureResult<int> ComplexAsync(int Base);

/** @brief ComplexDualAsync - 两个真异步 await 串行,函数内两次挂起 / 恢复。 */
MFUNCTION(Async)
SFutureResult<int> ComplexDualAsync(int Base);
