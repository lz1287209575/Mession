/**
 * @file MemberDemo.h
 * @brief MemberDemo - MCLASS 类成员 async 演示(方案 B:头纯声明,体在 .cpp)。
 *
 * 用来验证类成员 MFUNCTION(Async) 这条路径:Frame 生成 + 实现类外定义 + 调用。
 */

#pragma once

#include "Common/Runtime/Async/Awaitable.h"
#include "Common/Runtime/Async/MAsync.h"
#include "Common/Runtime/Concurrency/Promise.h"
#include "Common/Runtime/Reflect/Reflection.h"

/**
 * @brief RemoteFetch - await 目标(自由函数,模拟远端异步 I/O)。
 * @param V 输入值
 * @return ready 的 SFutureResult<int>,值为 V * 2
 */
SFutureResult<int> RemoteFetch(int V);

/**
 * @brief MemberService - 承载类成员 async 函数的 Service。
 */
MCLASS(Type = Service)
class MemberService {
    public:
    /**
     * @brief MemberAsync - 类成员 async 函数声明,实现由 codegen 生成。
     * @param Base 输入值;业务逻辑为 await RemoteFetch(Base) → 结果 + 1。
     */
    MFUNCTION(Async)
    SFutureResult<int> MemberAsync(int Base);
};

/**
 * @brief MAwaitTarget - 提供 await 目标成员方法的普通类。
 */
class MAwaitTarget {
    public:
    /**
     * @brief Fetch - await 目标成员方法(模拟异步 I/O)。
     * @param V 输入值
     * @return ready 的 SFutureResult<int>,值为 V * 2
     */
    SFutureResult<int> Fetch(int V);
};

/**
 * @brief MemberMethodAwait - MFUNCTION(Async) 声明,业务体在 MemberDemo.Async.cpp。
 *
 * 该用例的 await 目标是成员方法(TAwaitable<&MAwaitTarget::Fetch>),
 * 与 MemberService::MemberAsync 的自由函数目标形成对照。
 */
MFUNCTION(Async)
SFutureResult<int> MemberMethodAwait(int Base);
