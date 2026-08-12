#include "BranchDemo.h"

SFutureResult<int> AsyncAdd2(int A, int B)
{
    MPromise<TResult<int, FAppError>> P;
    P.SetValue(TResult<int, FAppError>::Ok(A + B));
    return SFutureResult<int>(P.GetFuture());
}

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
