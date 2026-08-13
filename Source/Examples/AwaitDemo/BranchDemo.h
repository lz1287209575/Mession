// BranchDemo — 控制流 await 演示（if 体内 await + early return）
#pragma once

#include "Common/Runtime/Reflect/Reflection.h"
#include "Common/Runtime/Async/Awaitable.h"
#include "Common/Runtime/Async/MAsync.h"
#include "Common/Runtime/Concurrency/Promise.h"

SFutureResult<int> AsyncAdd2(int A, int B);

MFUNCTION(Async)
SFutureResult<int> BranchAsync(int N);

MFUNCTION(Async)
SFutureResult<int> BranchElseAsync(int N);

MFUNCTION(Async)
SFutureResult<int> BranchMultiAwait(int N);

MFUNCTION(Async)
SFutureResult<int> BranchChainAsync(int N);

MFUNCTION(Async)
SFutureResult<int> NestedIfAsync(int N);

MFUNCTION(Async)
SFutureResult<int> BranchLoopAsync(int N);
