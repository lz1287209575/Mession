/**
 * @file AsyncDemoImpl.cpp
 * @brief AsyncDemo implementation - extracted for testing.
 *
 * Contains just the implementations of ComputeAsync and ChainAsync, without
 * the main() function. This allows the test target to link against these
 * implementations without conflicting with the test's own main().
 */
#include "AsyncDemo.h"

#include "Common/Runtime/Concurrency/Promise.h"
#include "Common/Runtime/Object/Result.h"

namespace AsyncDemoNS
{

SFutureResult<int> ComputeAsync(int Seed)
{
    // The local `Frame` is the implicit contract of AWAIT_OK
    // (AwaitMacros.h: macro expands to `Frame->AwaitOk(expr)`).
    auto Frame = MakeShared<MHeaderTool_AsyncFrame_Free_ComputeAsync>();

    // Ready-Ok path: build a synchronously-ready future carrying Seed*2.
    MPromise<TResult<int, FAppError>> Promise;
    Promise.SetValue(TResult<int, FAppError>::Ok(Seed * 2));

    // AWAIT_OK returns Awaited verbatim (P4 v1 pass-through AwaitOk),
    // so this matches the SFutureResult<int> return type.
    return AWAIT_OK(SFutureResult<int>(Promise.GetFuture()));
}

SFutureResult<int> ChainAsync(int Seed)
{
    auto Frame = MakeShared<MHeaderTool_AsyncFrame_Free_ChainAsync>();

    // Await the inner future. P4 v1: single-await pass-through — the
    // awaited future is captured in AwaitedSlot + StoredValue, so we
    // can read the resolved value synchronously when it is already ready.
    SFutureResult<int> InnerReturned = AWAIT_OK(ComputeAsync(Seed));
    const int Inner = InnerReturned.PeekResult().GetValue();

    // Derive the outer result: Inner + 1, ready-Ok.
    MPromise<TResult<int, FAppError>> Promise;
    Promise.SetValue(TResult<int, FAppError>::Ok(Inner + 1));

    return AWAIT_OK(SFutureResult<int>(Promise.GetFuture()));
}

} // namespace AsyncDemoNS