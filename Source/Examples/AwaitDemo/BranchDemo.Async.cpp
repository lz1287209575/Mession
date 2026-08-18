// BranchDemo.Async.cpp — async 业务逻辑体专用文件(约定:文件名 .Async.cpp = 天然 codegen 输入)
//
// 本文件由 MHeaderTool 解析(自动 -DMESSION_AWAIT_CODEGEN_SOURCE),业务编译不编译本文件
// (async 函数定义由 codegen 生成的 .mgenerated 状态机实现提供)。
// 约定:*.Async.cpp 只放 async 业务函数体(MFUNCTION(Async)),不写 #ifdef。

#include "BranchDemo.h"

MFUNCTION(Async)
SFutureResult<int> BranchElseAsync(int N)
{
    if (N > 0)
    {
        int R = TAwaitable<AsyncAdd2>(N, 1);                  // if 分支内 await
        return SFutureResult<int>(TResult<int, FAppError>::Ok(R));
    }
    else
    {
        int S = TAwaitable<AsyncAdd2>(N, 10);                 // else 分支内 await
        return SFutureResult<int>(TResult<int, FAppError>::Ok(S));
    }
}
MFUNCTION(Async)
SFutureResult<int> BranchMultiAwait(int N)
{
    if (N > 0)
    {
        int A = TAwaitable<AsyncAdd2>(N, 1);                 // if 分支内 await 1
        int B = TAwaitable<AsyncAdd2>(A, 2);                 // if 分支内 await 2（串行）
        return SFutureResult<int>(TResult<int, FAppError>::Ok(A + B));
    }
    return SFutureResult<int>(TResult<int, FAppError>::Ok(0));
}
MFUNCTION(Async)
SFutureResult<int> BranchChainAsync(int N)
{
    if (N > 10)
    {
        int A = TAwaitable<AsyncAdd2>(N, 1);                  // else-if 链分支 1 await
        return SFutureResult<int>(TResult<int, FAppError>::Ok(A));
    }
    else if (N > 0)
    {
        int B = TAwaitable<AsyncAdd2>(N, 2);                  // else-if 链分支 2 await
        return SFutureResult<int>(TResult<int, FAppError>::Ok(B));
    }
    else
    {
        int C = TAwaitable<AsyncAdd2>(N, 3);                  // 最后 else await
        return SFutureResult<int>(TResult<int, FAppError>::Ok(C));
    }
}
MFUNCTION(Async)
SFutureResult<int> NestedIfAsync(int N)
{
    if (N > 0)                        // 外层 if
    {
        if (N > 10)                   // 内层 if
        {
            int A = TAwaitable<AsyncAdd2>(N, 1);   // 内层 await
            return SFutureResult<int>(TResult<int, FAppError>::Ok(A * 2));
        }
        return SFutureResult<int>(TResult<int, FAppError>::Ok(5));   // 内层 early
    }
    return SFutureResult<int>(TResult<int, FAppError>::Ok(0));       // 外层 early
}
MFUNCTION(Async)
SFutureResult<int> BranchLoopAsync(int N)
{
    if (N > 0)                       // 外层 if
    {
        int Sum = 0;
        for (int i = 0; i < N; ++i)  // 分支内循环
        {
            Sum += TAwaitable<AsyncAdd2>(i, Sum);   // 循环内 await（累加）
        }
        return SFutureResult<int>(TResult<int, FAppError>::Ok(Sum));
    }
    return SFutureResult<int>(TResult<int, FAppError>::Ok(0));   // early
}
MFUNCTION(Async)
SFutureResult<int> DeepNestAsync(int N)
{
    if (N > 0)                        // 第 1 层
    {
        if (N > 10)                   // 第 2 层
        {
            if (N > 100)              // 第 3 层
            {
                int A = TAwaitable<AsyncAdd2>(N, 1);
                return SFutureResult<int>(TResult<int, FAppError>::Ok(A));
            }
            int B = TAwaitable<AsyncAdd2>(N, 2);
            return SFutureResult<int>(TResult<int, FAppError>::Ok(B));
        }
        return SFutureResult<int>(TResult<int, FAppError>::Ok(5));
    }
    return SFutureResult<int>(TResult<int, FAppError>::Ok(0));
}
MFUNCTION(Async)
SFutureResult<int> LoopIfAsync(int N)
{
    int Sum = 0;
    for (int i = 0; i < N; ++i)
    {
        if (i % 2 == 0)               // 循环内 if
        {
            Sum += TAwaitable<AsyncAdd2>(i, 1);   // if 内 await（偶数轮累加）
        }
    }
    return SFutureResult<int>(TResult<int, FAppError>::Ok(Sum));
}
MFUNCTION(Async)
SFutureResult<int> FallThroughAsync(int N)
{
    int Total = 0;
    if (N > 0)
    {
        Total = TAwaitable<AsyncAdd2>(N, 1);   // if 块内 await——无 return
    }
    Total = Total + 1;                         // if 后继续（fall-through）
    return SFutureResult<int>(TResult<int, FAppError>::Ok(Total));
}
MFUNCTION(Async)
SFutureResult<int> BranchAsync(int N)
{
    if (N > 0)
    {
        int R = TAwaitable<AsyncAdd2>(N, 1);                  // if 分支内 await
        return SFutureResult<int>(TResult<int, FAppError>::Ok(R * 2));
    }
    return SFutureResult<int>(TResult<int, FAppError>::Ok(0));   // early return（无 await）
}
