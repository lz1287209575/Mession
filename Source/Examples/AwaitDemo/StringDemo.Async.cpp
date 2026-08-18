// StringDemo.Async.cpp — async 业务逻辑体专用文件(约定:文件名 .Async.cpp = 天然 codegen 输入)
//
// 本文件由 MHeaderTool 解析(自动 -DMESSION_AWAIT_CODEGEN_SOURCE),业务编译不编译本文件
// (async 函数定义由 codegen 生成的 .mgenerated 状态机实现提供)。
// 约定:*.Async.cpp 只放 async 业务函数体(MFUNCTION(Async)),不写 #ifdef。

#include "StringDemo.h"

MFUNCTION(Async)
SFutureResult<MString> StringAsync(int V)
{
    MString S = TAwaitable<StringFetch>(V);                 // await 返回 MString
    return SFutureResult<MString>(TResult<MString, FAppError>::Ok(S + "!"));  // 业务逻辑
}
