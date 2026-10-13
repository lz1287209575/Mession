/**
 * @file AsyncDemo.h
 * @brief AsyncDemo - C++17 async/await 端到端示例的声明。
 *
 * 两个 namespace 级自由 async 函数(不挂 transport tag)。设计见
 * Docs/superpowers/examples/2026-07-29-cpp17-async-demo.md。
 */
#pragma once

#include "Common/Runtime/Async/AwaitMacros.h"
#include "Common/Runtime/Async/MAsync.h"

// 该 .mgenerated.h 由 MHeaderTool 的 P4 自由函数 pass 依据下面两个
// MFUNCTION(Async) 标记生成,其中声明 MHeaderTool_AsyncFrame_Free_ComputeAsync 与
// MHeaderTool_AsyncFrame_Free_ChainAsync 两个 Frame 类型。
//
// 包含顺序有硬性要求:它必须出现在 AWAIT_OK 宏展开之前,否则函数体里
// `auto Frame = MakeShared<...>()` 那句解析不到类型。
#include "AsyncDemo_FreeAsyncFrames.mgenerated.h"

namespace AsyncDemoNS {

    /**
     * @brief ComputeAsync - 内层 async,模拟一次 I/O 型 future(这里做成同步 ready)。
     *
     * @param Seed 种子值
     * @return ready 的 SFutureResult<int>,值为 Seed * 2
     *
     * @note P4 v1 限制:Frame 只是直通的 AwaitOk,没有真正的状态机。
     */
    MFUNCTION(Async)
    SFutureResult<int> ComputeAsync(int Seed);

    /**
     * @brief ChainAsync - 外层 async,用 AWAIT_OK 串起内层并派生新结果。
     *
     * @param Seed 传给 ComputeAsync 的种子值
     * @return ready 的 SFutureResult<int>,值为 Seed * 2 + 1
     */
    MFUNCTION(Async)
    SFutureResult<int> ChainAsync(int Seed);

} // namespace AsyncDemoNS
