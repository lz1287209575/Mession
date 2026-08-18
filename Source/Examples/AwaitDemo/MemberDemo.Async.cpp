// MemberDemo.Async.cpp — async 业务逻辑体专用文件(约定:文件名 .Async.cpp = 天然 codegen 输入)
//
// 本文件由 MHeaderTool 解析(自动 -DMESSION_AWAIT_CODEGEN_SOURCE),业务编译不编译本文件
// (async 函数定义由 codegen 生成的 .mgenerated 状态机实现提供)。
// 约定:*.Async.cpp 只放 async 业务函数体(MFUNCTION(Async)),不写 #ifdef。

#include "MemberDemo.h"

MFUNCTION(Async)
SFutureResult<int> MemberService::MemberAsync(int Base)
{
    int R = TAwaitable<RemoteFetch>(Base);                              // await 点
    return SFutureResult<int>(TResult<int, FAppError>::Ok(R + 1));      // 业务逻辑
}
