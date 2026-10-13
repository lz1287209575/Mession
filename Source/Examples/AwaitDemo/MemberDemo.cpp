/**
 * @file MemberDemo.cpp
 * @brief MemberDemo 的 await 目标实现(RemoteFetch / MAwaitTarget::Fetch)。
 */
#include "MemberDemo.h"

/**
 * @brief RemoteFetch - await 目标(自由函数),模拟远端异步 I/O。
 * @param V 输入值
 * @return ready 的 SFutureResult<int>,值为 V * 2
 */
SFutureResult<int> RemoteFetch(int V) {
    MPromise<TResult<int, FAppError>> P;
    P.SetValue(TResult<int, FAppError>::Ok(V * 2));
    return SFutureResult<int>(P.GetFuture());
}

/**
 * @brief MAwaitTarget::Fetch - await 目标成员方法,模拟异步 I/O。
 *
 * 由 TAwaitable<&MAwaitTarget::Fetch>(&Target, V) 这种成员函数指针形式调用,
 * 用于验证 codegen 对成员函数指针 await 目标的处理。
 *
 * @param V 输入值
 * @return ready 的 SFutureResult<int>,值为 V * 2
 */
SFutureResult<int> MAwaitTarget::Fetch(int V) {
    MPromise<TResult<int, FAppError>> P;
    P.SetValue(TResult<int, FAppError>::Ok(V * 2));
    return SFutureResult<int>(P.GetFuture());
}
