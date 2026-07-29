/**
 * @file AsyncDemo.cpp
 * @brief AsyncDemo - end-to-end C++17 async/await example implementation.
 *
 * Two free async functions + a main() that drives them through one
 * awaited inner future, one await chain, and one sync barrier (Wait()).
 * Prints `AsyncDemo: result = 43` on success. No event loop, no
 * service registry — runs as a plain executable on the main thread.
 */
#include "AsyncDemo.h"

#include "Common/Runtime/Concurrency/Promise.h"
#include "Common/Runtime/Object/Result.h"

#include <cstdio>

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

int main()
{
    auto F = AsyncDemoNS::ChainAsync(21);

    // Sync barrier — outside any Async function. With the ready-Ok
    // futures above this completes immediately; no event-loop thread
    // dependency, so MAsyncContext::IsSameContext redline does not fire.
    F.Wait();

    if (!F.IsOk())
    {
        // FAppError exposes `Code` and `Message` MStrings (Protocol/Messages/
        // Common/AppMessages.h:6-21) — no `What()` member. Use Message.
        std::printf("AsyncDemo: failed (%s)\n", F.GetError().Message.c_str());
        return 2;
    }

    const int Value = F.GetResult().GetValue();
    std::printf("AsyncDemo: result = %d\n", Value);
    return (Value == 43) ? 0 : 1;
}