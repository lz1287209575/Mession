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

#ifdef MESSION_AWAIT_CODEGEN_SOURCE
MFUNCTION(Async)
SFutureResult<int> ComplexChain(int Base)
{
    int Total = Base * 2;                                               // 业务逻辑 1
    int A = TAwaitable<AsyncAdd>(Total, 1);   // await 1
    Total = Total + A;                                                  // 业务逻辑 2（中间，Total/A 跨 await 存活）
    int B = TAwaitable<AsyncAdd>(Total, 2);   // await 2
    return SFutureResult<int>(TResult<int, FAppError>::Ok(Total + B));  // 业务逻辑 3
}
#endif

#ifdef MESSION_AWAIT_CODEGEN_SOURCE
MFUNCTION(Async)
SFutureResult<int> ComplexAsync(int Base)
{
    int R = TAwaitable<AsyncDelayed>(Base);  // await 异步目标——挂起，50ms 后恢复
    return SFutureResult<int>(TResult<int, FAppError>::Ok(R + 1));
}
#endif

#ifdef MESSION_AWAIT_CODEGEN_SOURCE
MFUNCTION(Async)
SFutureResult<int> ComplexDualAsync(int Base)
{
    int A = TAwaitable<AsyncDelayed>(Base);   // 挂起 → 50ms → 恢复（A = Base*2）
    int B = TAwaitable<AsyncDelayed>(A);      // 再挂起 → 50ms → 恢复（B = A*2）
    return SFutureResult<int>(TResult<int, FAppError>::Ok(A + B));
}
#endif

#ifdef MESSION_AWAIT_CODEGEN_SOURCE
MFUNCTION(Async)
SFutureResult<int> ComplexLoop(int N)
{
    int Sum = 0;
    for (int i = 0; i < N; ++i)
    {
        Sum += TAwaitable<AsyncAdd>(i, Sum);  // 循环内 await（累加）
    }
    return SFutureResult<int>(TResult<int, FAppError>::Ok(Sum));
}
#endif
