/**
 * @file StringDemo.h
 * @brief StringDemo - 非 int 返回类型(MString)的 await 验证。
 *
 * 生成器的 await 路径声称支持泛型 R(即任意 SFutureResult<R>),此前只拿 int 做过
 * 端到端验证,这里补上 MString 这条。
 */
#pragma once

#include "Common/Runtime/Async/Awaitable.h"
#include "Common/Runtime/Async/MAsync.h"
#include "Common/Runtime/Concurrency/Promise.h"
#include "Common/Runtime/Reflect/Reflection.h"

/**
 * @brief StringFetch - await 目标,返回已就绪的 SFutureResult<MString>,内容为 "v=<V>"。
 */
SFutureResult<MString> StringFetch(int V);

/**
 * @brief StringAsync - await StringFetch 后在其结果尾部追加 "!"。
 */
MFUNCTION(Async)
SFutureResult<MString> StringAsync(int V);
