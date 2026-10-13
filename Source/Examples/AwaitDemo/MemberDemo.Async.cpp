/**
 * @file MemberDemo.Async.cpp
 * @brief MemberDemo 的 async 业务逻辑体(codegen 输入文件)。
 *
 * 约定:文件名以 .Async.cpp 结尾即"天然 codegen 输入"。本文件由 MHeaderTool 解析
 * (自动带 -DMESSION_AWAIT_CODEGEN_SOURCE),**业务编译不编译它** —— 可编译的
 * async 函数定义由 codegen 生成的 .mgenerated 状态机实现提供。
 *
 * 约定:*.Async.cpp 里只放 async 业务函数体(MFUNCTION(Async)),不写 #ifdef。
 */

#include "MemberDemo.h"

MFUNCTION(Async)
SFutureResult<int> MemberService::MemberAsync(int Base) {
    int R = TAwaitable<RemoteFetch>(Base);                         // await 点
    return SFutureResult<int>(TResult<int, FAppError>::Ok(R + 1)); // 业务逻辑
}

MFUNCTION(Async)
SFutureResult<int> MemberMethodAwait(int Base) {
    MAwaitTarget Target;
    int          R = TAwaitable<&MAwaitTarget::Fetch>(&Target, Base); // await 成员方法(对象指针)
    return SFutureResult<int>(TResult<int, FAppError>::Ok(R + 1));    // 业务逻辑
}
