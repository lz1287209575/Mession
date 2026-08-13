#include "StringDemo.h"

#include <string>

SFutureResult<MString> StringFetch(int V)
{
    MPromise<TResult<MString, FAppError>> P;
    P.SetValue(TResult<MString, FAppError>::Ok("v=" + std::to_string(V)));
    return SFutureResult<MString>(P.GetFuture());
}

#ifdef MESSION_AWAIT_CODEGEN_SOURCE
MFUNCTION(Async)
SFutureResult<MString> StringAsync(int V)
{
    MString S = TAwaitable<StringFetch>(V);                 // await 返回 MString
    return SFutureResult<MString>(TResult<MString, FAppError>::Ok(S + "!"));  // 业务逻辑
}
#endif
