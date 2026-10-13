/**
 * @file BranchDemo.cpp
 * @brief BranchDemo 的 await 目标实现(AsyncAdd2)。
 */
#include "BranchDemo.h"

/**
 * @brief AsyncAdd2 - await 目标:返回已就绪的 A + B。
 * @param A 左操作数
 * @param B 右操作数
 */
SFutureResult<int> AsyncAdd2(int A, int B) {
    MPromise<TResult<int, FAppError>> P;
    P.SetValue(TResult<int, FAppError>::Ok(A + B));
    return SFutureResult<int>(P.GetFuture());
}
