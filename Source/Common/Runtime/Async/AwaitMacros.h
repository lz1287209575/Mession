#pragma once

#include "Common/Runtime/Async/AsyncContext.h"
#include "Common/Runtime/Async/MAsync.h"
#include "Common/Runtime/Object/Result.h"

#include <utility>

// =========================================================================
// AWAIT_OK(expr) — 状态机 Frame 内的 await 宏 (spec §7.1 / §7.3)
//
// 展开: Frame->AwaitOk(expr)
//
// 业务侧(P2 蓝图):
//   FSampleEchoResponse Resp = AWAIT_OK(MRpcChannel::Get().CallToActor<...>(...));
//
// - Ready 路径: 拆 TResult<T, FAppError>;Ok -> 返回 T;Err -> Outer 设 Err,
//   Frame 标记 "已 halt"
// - Pending 路径: 续体(Awaited.Then([Frame, Ctx](F){
//   Ctx->Post([Frame, F]{ Frame->Resume(); }); }))
//   + Frame 标记 "挂起中"
//
// spec §7.3 v1 子集: 单语句级 await、≤8/函数、顺序;for/while 内 await 禁;
// 不在 Async 内 Get() 依赖本 context 的 future(P1 §8.2 红线)。
//
// 注意: AWAIT_OK 仅供 Frame::Run() 内(以及 Frame::Resume() 内)调用 — 调用处
// 必须有局部变量名 `Frame`(这是宏展开的隐式契约)。Frame 是 P2 的 FEchoFrame。
// =========================================================================

#define AWAIT_OK(expr) Frame->AwaitOk(expr)