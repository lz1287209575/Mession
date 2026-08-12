// MemberDemo — MCLASS 类成员 async 演示（方案 B：头纯声明，体在 .cpp）
// 验证类成员 MFUNCTION(Async) 路径：Frame 生成 + Impl 类外定义 + 调用。

#pragma once

#include "Common/Runtime/Reflect/Reflection.h"
#include "Common/Runtime/Async/Awaitable.h"
#include "Common/Runtime/Async/MAsync.h"
#include "Common/Runtime/Concurrency/Promise.h"

// await 目标（自由函数，模拟远端异步 I/O）：返回 ready 的 SFutureResult<int>（V*2）
SFutureResult<int> RemoteFetch(int V);

MCLASS(Type=Service)
class MemberService
{
public:
    // 类成员 async 函数声明（MFUNCTION(Async)——codegen 生成实现）
    MFUNCTION(Async)
    SFutureResult<int> MemberAsync(int Base);
};
