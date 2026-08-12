// ComplexDemo — 复杂 await 演示业务逻辑体（codegen 输入）+ await 目标实现
#include "ComplexDemo.h"

// await 目标实现（模拟异步 I/O：ready future，A + B）
SFutureResult<int> AsyncAdd(int A, int B)
{
    MPromise<TResult<int, FAppError>> P;
    P.SetValue(TResult<int, FAppError>::Ok(A + B));
    return SFutureResult<int>(P.GetFuture());
}

#ifdef MESSION_AWAIT_CODEGEN_SOURCE
MFUNCTION(Async)
SFutureResult<int> ComplexChain(int Base)
{
    int Total = Base * 2;                                               // 业务逻辑 1
    int A = TAwaitable<decltype(&AsyncAdd), int, int, int>(Total, 1);   // await 1
    Total = Total + A;                                                  // 业务逻辑 2（中间，Total/A 跨 await 存活）
    int B = TAwaitable<decltype(&AsyncAdd), int, int, int>(Total, 2);   // await 2
    return SFutureResult<int>(TResult<int, FAppError>::Ok(Total + B));  // 业务逻辑 3
}
#endif

#ifdef MESSION_AWAIT_CODEGEN_SOURCE
MFUNCTION(Async)
SFutureResult<int> ComplexLoop(int N)
{
    int Sum = 0;
    for (int i = 0; i < N; ++i)
    {
        Sum += TAwaitable<decltype(&AsyncAdd), int, int, int>(i, Sum);  // 循环内 await（累加）
    }
    return SFutureResult<int>(TResult<int, FAppError>::Ok(Sum));
}
#endif
