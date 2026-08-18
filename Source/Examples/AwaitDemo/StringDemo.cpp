#include "StringDemo.h"

#include <string>

SFutureResult<MString> StringFetch(int V)
{
    MPromise<TResult<MString, FAppError>> P;
    P.SetValue(TResult<MString, FAppError>::Ok("v=" + std::to_string(V)));
    return SFutureResult<MString>(P.GetFuture());
}
