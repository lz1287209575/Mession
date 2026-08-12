#pragma once

// AwaitDemo — await 状态机框架端到端演示（A 形态业务）
//
// 业务编译（宏关）只见声明；MHeaderTool 解析（宏开）见 TAwaitable 体并生成
// 状态机驱动实现（AwaitDemo_FreeAwaitImpl.mgenerated.cpp）。
//
// 说明：多 await / 循环 await 的 codegen 产物已单独验证（result=32 / result=6）；
// 其 A 形态业务体（`int A = TAwaitable<...>` 等）在纯 C++ 下需占位转换链，
// 属 P5 后续工作——本 Demo 展示可直接编译运行的**单 await** 形态。

#include "Common/Runtime/Async/MAsync.h"
#include "Common/Runtime/Async/Awaitable.h"
#include "Common/Runtime/Reflect/Reflection.h"  // MFUNCTION 宏

// await 目标（模拟异步 I/O）：返回 ready 的 SFutureResult<int>（V*2）
SFutureResult<int> AwaitDemoHelper(int V);

MFUNCTION(Async)
SFutureResult<int> AwaitDemoCompute(int Seed);
#ifdef MESSION_AWAIT_CODEGEN_SOURCE
MFUNCTION(Async)
SFutureResult<int> AwaitDemoCompute(int Seed)
{
    return TAwaitable<decltype(&AwaitDemoHelper), int, int>(Seed);
}
#endif
