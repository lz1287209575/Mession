// ComplexDemo — 复杂 await 演示业务逻辑体（codegen 输入）+ await 目标实现
#include "ComplexDemo.h"

#include <chrono>
#include <cstdio>
#include <thread>

// await 目标实现（模拟异步 I/O：ready future，A + B）
SFutureResult<int> AsyncAdd(int A, int B)
{
    MPromise<TResult<int, FAppError>> P;
    P.SetValue(TResult<int, FAppError>::Ok(A + B));
    return SFutureResult<int>(P.GetFuture());
}

// await 目标实现（真正异步）：延迟 50ms 完成——完成时触发状态机恢复（Frame->Resume）
SFutureResult<int> AsyncDelayed(int V)
{
    auto P = MakeShared<MPromise<TResult<int, FAppError>>>();
    std::thread([P, V]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        P->SetValue(TResult<int, FAppError>::Ok(V * 2));  // 完成 → Then 回调 → Frame->Resume()
    }).detach();
    return SFutureResult<int>(P->GetFuture());
}
