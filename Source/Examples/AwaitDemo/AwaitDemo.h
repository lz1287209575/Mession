/**
 * @file AwaitDemo.h
 * @brief await 状态机框架端到端演示的声明(C# async 模型,方案 B)。
 *
 * 业务头只做纯声明(和普通函数一样);业务逻辑体(含 await 表达式)放在 .cpp 里,
 * 作为 codegen 输入 —— MHeaderTool 解析 .cpp 时收集 TAwaitable 站点。
 */

#pragma once

#include "Common/Runtime/Async/Awaitable.h"
#include "Common/Runtime/Async/MAsync.h"
#include "Common/Runtime/Concurrency/Promise.h"
#include "Common/Runtime/Reflect/Reflection.h"

/**
 * @brief AwaitDemoHelper - await 目标(模拟异步 I/O)。
 * @param V 输入值
 * @return ready 的 SFutureResult<int>,值为 V * 2
 */
SFutureResult<int> AwaitDemoHelper(int V);

/**
 * @brief AwaitDemoCompute - MFUNCTION(Async) 声明。
 *
 * 这里只有声明:可编译实现由 codegen 生成,业务逻辑体在 AwaitDemo.Async.cpp。
 *
 * @param Seed 种子值;业务逻辑为 Seed * 3 → await AwaitDemoHelper → 结果 + 1。
 */
MFUNCTION(Async)
SFutureResult<int> AwaitDemoCompute(int Seed);
