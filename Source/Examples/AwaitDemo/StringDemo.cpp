/**
 * @file StringDemo.cpp
 * @brief StringDemo 的 await 目标实现(StringFetch)。
 */
#include "StringDemo.h"

#include <string>

/**
 * @brief StringFetch - await 目标,返回已就绪的 "v=<V>"。
 * @param V 输入值,参与拼接结果字符串
 */
SFutureResult<MString> StringFetch(int V) {
    MPromise<TResult<MString, FAppError>> P;
    P.SetValue(TResult<MString, FAppError>::Ok("v=" + std::to_string(V)));
    return SFutureResult<MString>(P.GetFuture());
}
