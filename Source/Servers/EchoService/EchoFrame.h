#pragma once

#include "Common/Runtime/Async/AsyncContext.h"
#include "Common/Runtime/Async/AwaitMacros.h"
#include "Common/Runtime/Async/MAsync.h"
#include "Common/Runtime/Concurrency/Promise.h"
#include "Protocol/Messages/EchoService/FSampleEchoMessages.h"

#include <cassert>
#include <utility>

class MEchoService;

// =========================================================================
// FEchoFrame — P2 单-await 续体-based Frame,演示 AWAIT_OK 宏。
//
// v1 简化:
//  - 单 await 站点(只有 CallToActor 一个)
//  - 续体存在 AwaitOk 闭包捕获 this
//  - Halted 状态对外不可见(Outer 是 Result-包装 SFutureResult,Err 自动传播)
//
// P3 多 await 时需把 Resume 改为状态机索引(本类不在 P3 范围内)。
// =========================================================================
class FEchoFrame
{
public:
    FEchoFrame();

    // 入口: 由 MEchoService::EchoAwait 调用。
    SFutureResult<FSampleEchoResponse> Run(const FSampleEchoRequest& Request,
                                           MEchoService* Service);

    // AWAIT_OK 宏展开的调用点 — Frame 成员函数。
    // - Ready: 拆 Result;Ok -> 返回 T;Err -> Outer 设 Err + 标记 Halted
    // - Pending: 续体(Awaited.Then(Then+Post Resume));返回 T{} 占位
    template<typename T>
    T AwaitOk(SFutureResult<T> Awaited);

    // Resume 由 AwaitOk 注册的闭包调(MAsyncContext::Post 后)。多次安全。
    void Resume();

    // Outer SFutureResult, Run 立刻返回(pending 或 ready)。
    // NOTE: declared after OuterPromise so the ctor can read it during init.
    // Use Outer() to retrieve — direct field access stays private.
    SFutureResult<FSampleEchoResponse> Outer() const { return OuterFuture; }

    // ---- Test seams (P2 vertical-slice unit test) ----
    // State machine is otherwise black-box. These let unit tests drive the
    // Ready-Ok / Ready-Err / Pending paths without standing up real network.
    bool IsHalted() const { return bHalted; }
    bool PendingIsSet() const { return Pending.Valid(); }

    // Drives the post-AWAIT_OK tail of Run() — public so unit tests can
    // exercise the same path without invoking MRpcChannel. No-op for the
    // pending branch (Resume will fire when the awaited future resolves).
    void CompleteAfterAwaitForTest(FSampleEchoResponse OkValue)
    {
        if (bHalted)
        {
            OuterPromise.SetValue(PendingError);
        }
        else if (Pending.Valid() && Pending.IsReady())
        {
            // Pending completed before we got here (Then ran synchronously);
            // Resume already set OuterPromise.
        }
        else
        {
            OuterPromise.SetValue(
                TResult<FSampleEchoResponse, FAppError>::Ok(std::move(OkValue)));
        }
    }

private:
    MPromise<TResult<FSampleEchoResponse, FAppError>> OuterPromise;
    SFutureResult<FSampleEchoResponse> OuterFuture;
    MAsync::MAsyncContext* Ctx = nullptr;
    SFutureResult<FSampleEchoResponse> Pending;       // 单 await
    MEchoService* Service = nullptr;
    FSampleEchoRequest Request;
    bool bHalted = false;
    TResult<FSampleEchoResponse, FAppError> PendingError;
};