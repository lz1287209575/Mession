# AsyncDemo — End-to-End C++17 Async Example

**Date:** 2026-07-29
**Spec:** `Docs/superpowers/specs/2026-07-29-async-demo-example-design.md`
**Code:** `Source/Examples/AsyncDemo/`
**Audience:** an engineer who has read the parent async spec (§1–§9 of `2026-07-24-cpp17-async-await.md`) and wants to see the whole pipeline — header marker → codegen → generated Frame struct → awaited chain → sync barrier — in one small program.

## 1. What this is

`AsyncDemo` is the smallest program that still exercises every moving part of the C++17 async/await model introduced by P0–P4: one `MFUNCTION(Async)` free function, one awaited inner future, one await chain, one sync barrier, one printed result. It is **deliberately tiny** — no service registry, no EchoService, no Gateway, no event-loop thread. You can read it top-to-bottom in 5 minutes and see the contract.

It is **not** a toy: every line goes through the same MHeaderTool codegen pipeline that real services use, so what you read here is what production code looks like (minus the transport tags).

## 2. Build & run

By default (`-DBUILD_ASYNC_DEMO=ON`, the project default), the example is included in the regular build:

```bash
# Configure + build (full project)
cmake -S /root/Mession -B /root/Mession/Build -DCMAKE_BUILD_TYPE=Release
cmake --build /root/Mession/Build --target AsyncDemo -j4

# Run
/root/Mession/Bin/AsyncDemo
# stdout:  AsyncDemo: result = 43
# exit:    0

# Run the smoke tests
cmake --build /root/Mession/Build --target AsyncDemoTest -j4
/root/Mession/Bin/AsyncDemoTest
# stdout:  === Results: 13 passed, 0 failed ===
```

To fully decouple the example from the project, configure with `-DBUILD_ASYNC_DEMO=OFF`. The example's own CMake file (`Source/Examples/AsyncDemo/CMakeLists.txt`) is self-contained — it links only `mession_common`, no service registry, no event loop, no transport, no `mession_netdriver`. The same is true for `AsyncDemoTest`.

There is no Registry to start, no `Scripts/servers.py`, no `--registry=...` flag.

## 3. Step-by-step walk

### `AsyncDemo.h` — the declarations

```cpp
#include "Common/Runtime/Async/MAsync.h"           // SFutureResult<T>, MPromise, FAppError
#include "Common/Runtime/Async/AwaitMacros.h"      // AWAIT_OK(expr) → Frame->AwaitOk(expr)
#include "AsyncDemo_FreeAsyncFrames.mgenerated.h"  // GENERATED — the two Frame structs

namespace AsyncDemoNS {

MFUNCTION(Async)                                   // MHeaderTool picks this up
SFutureResult<int> ComputeAsync(int Seed);         // Inner async: Seed*2

MFUNCTION(Async)
SFutureResult<int> ChainAsync(int Seed);           // Outer async: AWAIT_OK(ComputeAsync(Seed)) + 1

} // namespace AsyncDemoNS
```

Three things to notice:

1. `MFUNCTION(Async)` on a **free function** — no `ServerCall`, `ClientCall`, or other transport tag. MHeaderTool rejects transport tags on free functions (P4 §B) because transport is a class-method concept.
2. The generated include path `AsyncDemo_FreeAsyncFrames.mgenerated.h` lives next to your header in `Build/Generated/`. MHeaderTool emits it on every codegen run; you must `#include` it yourself so the `AWAIT_OK` macro can resolve the local `Frame` symbol's `MHeaderTool_AsyncFrame_Free_<Func>` type.
3. The return type is always `SFutureResult<T>` — the single contract for async functions (spec §5.1).

### `AsyncDemo.cpp` — the definitions

```cpp
SFutureResult<int> ComputeAsync(int Seed)
{
    auto Frame = MakeShared<MHeaderTool_AsyncFrame_Free_ComputeAsync>();
    MPromise<TResult<int, FAppError>> Promise;
    Promise.SetValue(TResult<int, FAppError>::Ok(Seed * 2));
    return AWAIT_OK(SFutureResult<int>(Promise.GetFuture()));
}

SFutureResult<int> ChainAsync(int Seed)
{
    auto Frame = MakeShared<MHeaderTool_AsyncFrame_Free_ChainAsync>();
    SFutureResult<int> InnerReturned = AWAIT_OK(ComputeAsync(Seed));
    const int Inner = InnerReturned.PeekResult().GetValue();
    MPromise<TResult<int, FAppError>> Promise;
    Promise.SetValue(TResult<int, FAppError>::Ok(Inner + 1));
    return AWAIT_OK(SFutureResult<int>(Promise.GetFuture()));
}
```

Two things to notice:

1. **`Frame` must be a local** — `AWAIT_OK` expands to `Frame->AwaitOk(expr)` (see `Common/Runtime/Async/AwaitMacros.h`). The Frame is the implicit state carrier for the function's await points.
2. **P4 v1 single-await simplification** — `AwaitOk` is a pass-through: it returns the awaited future verbatim, snapshots it into `AwaitedSlot`, and records the Ok value into `StoredValue`. The user body reads `PeekResult()` because the future is already ready by the time `AWAIT_OK` returns. P5+ will introduce a real state machine (see §6).

### `main()` — the sync barrier

```cpp
int main()
{
    auto F = AsyncDemoNS::ChainAsync(21);   // 21 -> ComputeAsync(21) -> 42 -> +1 -> 43
    F.Wait();                                // sync barrier outside any Async function
    if (!F.IsOk()) { return 2; }
    std::printf("AsyncDemo: result = %d\n", F.GetResult().GetValue());
    return (F.GetResult().GetValue() == 43) ? 0 : 1;
}
```

The sync barrier is **outside** any `Async` function and runs on the main thread (no `MAsyncContext::Current()` is active), so the P1 §8.2 redline — "do not `Get()` a future that depends on the event loop you are running on" — does not fire. `Wait()` is the supported off-loop sync primitive (spec §8.2; CLAUDE.md async quick-reference).

## 4. What the codegen produces

`Build/Generated/AsyncDemo_FreeAsyncFrames.mgenerated.h` contains:

```cpp
#pragma once
// Generated by MHeaderTool
// Source: /root/Mession/Source/Examples/AsyncDemo/AsyncDemo.h
// Free Async Frame struct definitions (P4 — spec 2026-07-28 §B)

#include "Common/Runtime/Async/MAsync.h"
#include "Common/Runtime/Reflect/Reflection.h"

struct MHeaderTool_AsyncFrame_Free_ComputeAsync
{
    SFutureResult<int> AwaitedSlot;
    int StoredValue {};

    SFutureResult<int> AwaitOk(SFutureResult<int> Awaited)
    {
        AwaitedSlot = Awaited;
        if (Awaited.IsReady())
        {
            const auto& R = Awaited.PeekResult();
            if (R.IsOk()) StoredValue = R.GetValue();
        }
        return Awaited;
    }
};

struct MHeaderTool_AsyncFrame_Free_ChainAsync { /* same shape, response type = int */ };
```

The naming is `MHeaderTool_AsyncFrame_Free_<FuncName>` — parallel to the class-method `MHeaderTool_AsyncFrame_<Class>_<Func>` pattern in `Build/Generated/MEchoService_AsyncFrames.h`. Both shapes share the `AwaitOk` pass-through contract, so the `AWAIT_OK` macro works unchanged across free functions and class methods.

The `MakeShared<MHeaderTool_AsyncFrame_Free_ComputeAsync>()` line in `ComputeAsync` is what gives `AWAIT_OK` a `Frame` to call `AwaitOk` on.

## 5. What this example deliberately does NOT cover

Each of these is out of scope here; the real code in the repository covers them.

- **Class-method async** (`MFUNCTION(..., Async)` on a `MCLASS`) — see `Source/Servers/EchoService/Tests/main.cpp` (P3 `AsyncFrameTest`).
- **Transport tags** (`MFUNCTION(ServerCall, Async)` / `ClientCall` / `RPC`) — see `Source/Servers/EchoService/EchoService.h` + `Source/Servers/Gateway/GatewayServer.h`.
- **Cross-instance RPC** (`MRpcChannel::CallToActor`) — requires `MServiceRegistry` + `MEndpointCache`, see `Source/Common/Net/ServiceDiscovery/` + `Source/Common/Net/Rpc/MRpcChannel.h`.
- **Multi-await state machines** — P5 territory. P4's `AwaitOk` is a pass-through; P5 will split the Frame into per-await-state slots and a real continuation.
- **Event-loop thread assertions** (`MAsyncContext::IsSameContext` redline, spec §8.2) — `AsyncDemo` runs on the main thread with no `MAsyncContext`; the redline never fires. The P1 test (`MAsyncTest`) covers the on-loop assert path.
- **Error propagation paths beyond a single `IsOk()` check** — both futures in this example are synchronously ready-Ok. `Source/Common/Runtime/Async/Tests/main.cpp` covers `Err` futures.

## 6. P5+ roadmap

- **Multi-await state machines** — the Frame struct grows per-state slots; `AwaitOk` becomes a state-transition primitive instead of a pass-through. The `AWAIT_OK` macro contract stays the same.
- **`MASYNC` successor decisions** — P0–P3 considered a `MASYNC` macro for namespace-scope async helpers; P4 settled on `MFUNCTION(Async)` covering both class methods and free functions. No `MASYNC` introduction planned.
- **Cancellation / `MAsyncContext::Cancel()` integration** — the Frame will learn to react to context cancellation and unwind its state chain.
- **Coroutine interop** — possibly bridging to C++20 coroutines for select call sites; not on the near-term roadmap.

---

**See also:**
- Parent spec: `Docs/superpowers/specs/2026-07-24-cpp17-async-await.md`
- P4 wrap spec: `Docs/superpowers/specs/2026-07-28-async-p4-wrap.md`
- AsyncDemo design spec: `Docs/superpowers/specs/2026-07-29-async-demo-example-design.md`
- `CLAUDE.md` "C++17 async model — quick reference"