// ComplexDemo.Async.cpp — async 业务逻辑体专用文件(约定:文件名 .Async.cpp = 天然 codegen 输入)
//
// 本文件由 MHeaderTool 解析(自动 -DMESSION_AWAIT_CODEGEN_SOURCE),业务编译不编译本文件
// (async 函数定义由 codegen 生成的 .mgenerated 状态机实现提供)。
// 约定:*.Async.cpp 只放 async 业务函数体(MFUNCTION(Async)),不写 #ifdef。

#include "ComplexDemo.h"

MFUNCTION(Async)
SFutureResult<int> ComplexChain(int Base)
{
    int Total = Base * 2;                                               // 业务逻辑 1
    int A = TAwaitable<AsyncAdd>(Total, 1);   // await 1
    Total = Total + A;                                                  // 业务逻辑 2（中间，Total/A 跨 await 存活）
    int B = TAwaitable<AsyncAdd>(Total, 2);   // await 2
    return SFutureResult<int>(TResult<int, FAppError>::Ok(Total + B));  // 业务逻辑 3
}
MFUNCTION(Async)
SFutureResult<int> ComplexAsync(int Base)
{
    int R = TAwaitable<AsyncDelayed>(Base);  // await 异步目标——挂起，50ms 后恢复
    return SFutureResult<int>(TResult<int, FAppError>::Ok(R + 1));
}
MFUNCTION(Async)
SFutureResult<int> ComplexDualAsync(int Base)
{
    int A = TAwaitable<AsyncDelayed>(Base);   // 挂起 → 50ms → 恢复（A = Base*2）
    int B = TAwaitable<AsyncDelayed>(A);      // 再挂起 → 50ms → 恢复（B = A*2）
    return SFutureResult<int>(TResult<int, FAppError>::Ok(A + B));
}
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
