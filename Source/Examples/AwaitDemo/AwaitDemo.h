// AwaitDemo — await 状态机框架端到端演示（C# async 模型）
// 业务函数体 = 正常业务逻辑 + await 表达式（#ifdef 保护，codegen 的输入）；
// 编译版实现（含业务逻辑段）由 MHeaderTool 生成并独立编译链接。

#pragma once

#include "Common/Runtime/Reflect/Reflection.h"  // MFUNCTION 宏
#include "Common/Runtime/Async/Awaitable.h"
#include "Common/Runtime/Async/MAsync.h"
#include "Common/Runtime/Concurrency/Promise.h"

// await 目标（模拟异步 I/O）：返回 ready 的 SFutureResult<int>（V*2）
SFutureResult<int> AwaitDemoHelper(int V);

// MFUNCTION(Async) 声明
MFUNCTION(Async)
SFutureResult<int> AwaitDemoCompute(int Seed);

// 业务逻辑体（codegen 输入）：正常业务代码 + await 点
#ifdef MESSION_AWAIT_CODEGEN_SOURCE
MFUNCTION(Async)
SFutureResult<int> AwaitDemoCompute(int Seed)
{
    int Mid = Seed * 3;                                             // 正常业务逻辑 1
    int R = TAwaitable<decltype(&AwaitDemoHelper), int, int>(Mid);  // await 点
    return SFutureResult<int>(TResult<int, FAppError>::Ok(R + 1));  // 正常业务逻辑 2
}
#endif
