// MemberDemo — 类成员 async 业务逻辑体（codegen 输入）+ await 目标实现
#include "MemberDemo.h"

// await 目标实现（模拟远端异步 I/O：ready future，V*2）
SFutureResult<int> RemoteFetch(int V)
{
    MPromise<TResult<int, FAppError>> P;
    P.SetValue(TResult<int, FAppError>::Ok(V * 2));
    return SFutureResult<int>(P.GetFuture());
}

// 类成员 async 业务逻辑体（codegen 输入）
#ifdef MESSION_AWAIT_CODEGEN_SOURCE
MFUNCTION(Async)
SFutureResult<int> MemberService::MemberAsync(int Base)
{
    int R = TAwaitable<RemoteFetch>(Base);                              // await 点
    return SFutureResult<int>(TResult<int, FAppError>::Ok(R + 1));      // 业务逻辑
}
#endif
