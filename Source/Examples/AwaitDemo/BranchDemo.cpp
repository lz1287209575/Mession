#include "BranchDemo.h"

SFutureResult<int> AsyncAdd2(int A, int B)
{
    MPromise<TResult<int, FAppError>> P;
    P.SetValue(TResult<int, FAppError>::Ok(A + B));
    return SFutureResult<int>(P.GetFuture());
}

#ifdef MESSION_AWAIT_CODEGEN_SOURCE
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
#endif

#ifdef MESSION_AWAIT_CODEGEN_SOURCE
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
#endif

#ifdef MESSION_AWAIT_CODEGEN_SOURCE
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
#endif

#ifdef MESSION_AWAIT_CODEGEN_SOURCE
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
#endif
