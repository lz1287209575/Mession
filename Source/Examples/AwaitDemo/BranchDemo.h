/**
 * @file BranchDemo.h
 * @brief BranchDemo - 控制流场景的 await 演示(if 体内 await + early return)。
 *
 * 这里声明的一批 async 函数只用于覆盖 codegen 的各种控制流分支:分支内 await、
 * 分支内多 await、嵌套 if、分支内循环、循环内 if、fall-through 等。业务逻辑体在
 * BranchDemo.Async.cpp,可编译实现由 MHeaderTool 生成。
 */
#pragma once

#include "Common/Runtime/Async/Awaitable.h"
#include "Common/Runtime/Async/MAsync.h"
#include "Common/Runtime/Concurrency/Promise.h"
#include "Common/Runtime/Reflect/Reflection.h"

/**
 * @brief AsyncAdd2 - await 目标:返回已就绪的 A + B。
 */
SFutureResult<int> AsyncAdd2(int A, int B);

/** @brief BranchAsync - if 分支内 await,不命中时 early return 0。 */
MFUNCTION(Async)
SFutureResult<int> BranchAsync(int N);

/** @brief BranchElseAsync - if / else 两个分支内各有一个 await。 */
MFUNCTION(Async)
SFutureResult<int> BranchElseAsync(int N);

/** @brief BranchMultiAwait - if 分支内两个 await 串行执行。 */
MFUNCTION(Async)
SFutureResult<int> BranchMultiAwait(int N);

/** @brief BranchChainAsync - else-if 链,三个分支各有一个 await。 */
MFUNCTION(Async)
SFutureResult<int> BranchChainAsync(int N);

/** @brief NestedIfAsync - 嵌套 if,只有内层命中时才走 await。 */
MFUNCTION(Async)
SFutureResult<int> NestedIfAsync(int N);

/** @brief BranchLoopAsync - 分支内的循环,循环体里 await 累加。 */
MFUNCTION(Async)
SFutureResult<int> BranchLoopAsync(int N);

/** @brief DeepNestAsync - 三层嵌套 if,按 N 的大小决定在第几层 await。 */
MFUNCTION(Async)
SFutureResult<int> DeepNestAsync(int N);

/** @brief LoopIfAsync - 循环体内套 if,只有偶数轮才 await 累加。 */
MFUNCTION(Async)
SFutureResult<int> LoopIfAsync(int N);

/** @brief FallThroughAsync - if 块内 await 但无 return,恢复后继续走外层(fall-through)。 */
MFUNCTION(Async)
SFutureResult<int> FallThroughAsync(int N);
