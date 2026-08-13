// StringDemo — 非 int 返回类型（MString）await 验证
// 生成器声称泛型 R（任意 SFutureResult<R>）——补端到端验证（此前只测过 int）。
#pragma once

#include "Common/Runtime/Reflect/Reflection.h"
#include "Common/Runtime/Async/Awaitable.h"
#include "Common/Runtime/Async/MAsync.h"
#include "Common/Runtime/Concurrency/Promise.h"

// await 目标：返回 ready 的 SFutureResult<MString>（"v=<V>"）
SFutureResult<MString> StringFetch(int V);

MFUNCTION(Async)
SFutureResult<MString> StringAsync(int V);
