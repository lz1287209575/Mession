# AsyncDemo — End-to-End Async Model Example (P4 wrap-up)

**Date:** 2026-07-29
**Status:** draft (pending user approval)
**Scope:** A standalone, runnable end-to-end example that exercises the full C++17 async/await pipeline introduced by P0–P4. **One binary**, **one async free function**, **one awaited inner future**, **one sync barrier**, **one printed result**.

## Motivation

P4 deleted legacy APIs, added free-function codegen, and synced the parent spec — but a new reader still has to stitch together six files (`EchoService.h`, `MEchoService_AsyncFrames.h`, `AsyncFrameTest`, the codegen target wiring, etc.) to see "what does the async model actually look like?" `AsyncDemo` answers that with **a single small program** they can read top-to-bottom in 5 minutes.

It must be:

1. **Self-contained** — no service registry, no EchoService, no Gateway, no event-loop threads. Just one executable that builds, runs, and prints a value.
2. **Build-driven codegen** — same `MFUNCTION(Async)` marker → `Build/Generated/...` pipeline as real services, so the example is honest about how the framework actually works (not a hand-written fake).
3. **Honest about P4 v1 limits** — declares a Frame but only does one await (state-machine split is P5+). Document this in the doc, don't hide it.

## Location

- Source: `Source/Examples/AsyncDemo/`
  - `AsyncDemo.h` — declarations: two `MFUNCTION(Async)` free functions
  - `AsyncDemo.cpp` — definitions + a `main()` that drives the example
  - `Tests/main.cpp` — `EXPECT_TRUE` smoke assertions using `TestHarness.h`
- CMake target: `AsyncDemo` (executable) added in top-level `CMakeLists.txt` near `AsyncFrameTest`, plus `add_dependencies(AsyncDemo mession_reflection_codegen)` and `mession_attach_generated_groups(AsyncDemo shared)`
- Doc: `Docs/superpowers/examples/2026-07-29-cpp17-async-demo.md` — explains every step
- Spec pointer: parent spec §18 附录 A gets a one-line entry pointing here

## What the example exercises (minimum viable)

**`AsyncDemo.h`:**

```cpp
#pragma once
#include "Common/Runtime/Async/MAsync.h"
#include "AsyncDemo_FreeAsyncFrames.mgenerated.h"  // generated; MHeaderTool scans here

namespace AsyncDemoNS
{

// Inner async — simulates an I/O-style future that may be ready or pending.
MFUNCTION(Async)
SFutureResult<int> ComputeAsync(int Seed);

// Outer async — chains the inner one via AWAIT_OK and returns a derived value.
MFUNCTION(Async)
SFutureResult<int> ChainAsync(int Seed);

} // namespace AsyncDemoNS
```

**`AsyncDemo.cpp`:**

```cpp
#include "AsyncDemo.h"
#include "Common/Runtime/Concurrency/Promise.h"

namespace AsyncDemoNS
{

SFutureResult<int> ComputeAsync(int Seed)
{
    auto Frame = MakeShared<MHeaderTool_AsyncFrame_Free_ComputeAsync>();
    MPromise<TResult<int, FAppError>> P;
    P.SetValue(TResult<int, FAppError>::Ok(Seed * 2));
    return AWAIT_OK(SFutureResult<int>(P.GetFuture()));
}

SFutureResult<int> ChainAsync(int Seed)
{
    auto Frame = MakeShared<MHeaderTool_AsyncFrame_Free_ChainAsync>();
    int Inner = AWAIT_OK(ComputeAsync(Seed));
    return AWAIT_OK(/* derived SFutureResult<int> */);  // Wrap (Inner + 1) in ready-Ok
}

} // namespace AsyncDemoNS

int main()
{
    auto F = AsyncDemoNS::ChainAsync(21);
    // Sync barrier — outside any Async function.
    F.Wait();
    if (F.IsOk())
    {
        std::printf("AsyncDemo: result = %d\n", F.GetResult().GetValue());
        return (F.GetResult().GetValue() == 43) ? 0 : 1;
    }
    return 2;
}
```

**`Tests/main.cpp`** — same scenario as `main`, but wrapped in `TEST_CASE(...)` + `EXPECT_TRUE` from `TestHarness.h`. Returns `RUN_TESTS();`.

## What the example explicitly does NOT cover

This is a minimal demo. These are out of scope (each would deserve its own example):

- Class-method async (`MFUNCTION(..., Async)` on a `MCLASS`) — covered by existing `AsyncFrameTest` + `EchoService`.
- `ServerCall` / `ClientCall` transport — covered by `EchoService` + `MClientManifest`.
- Cross-instance `MRpcChannel` calls — requires service registry, out of scope for a standalone demo.
- Multi-await state machine — **P5 territory** (the doc states this explicitly).
- `MAsyncContext::IsSameContext` redline (event-loop thread assertion) — example runs on the main thread, no event loop.
- Error propagation paths beyond a single `IsOk()` check.

The companion doc lists each "what this doesn't show" item with a pointer to the real code that does.

## Build pipeline

1. CMake reconfigures — `mession_reflection_codegen` runs `MHeaderTool` over `Source/` (recursively), which finds `AsyncDemo.h`, parses both `MFUNCTION(Async)` markers, and emits `Build/Generated/AsyncDemo_FreeAsyncFrames.mgenerated.h` containing `MHeaderTool_AsyncFrame_Free_ComputeAsync` + `MHeaderTool_AsyncFrame_Free_ChainAsync`.
2. `AsyncDemo` executable links against `mession_common` + generated-group object library.
3. Running `/root/Mession/Bin/AsyncDemo` prints `AsyncDemo: result = 43` and exits 0.

## Companion doc structure

`Docs/superpowers/examples/2026-07-29-cpp17-async-demo.md` — ~150 lines, sections:

1. **What this is** — one paragraph, links back to parent spec §1.
2. **Build & run** — 3 commands (`cmake --build`, `./Bin/AsyncDemo`).
3. **Step-by-step walk** — copy `AsyncDemo.h` + `AsyncDemo.cpp`, explain each line.
4. **What the codegen produces** — show actual `Build/Generated/AsyncDemo_FreeAsyncFrames.mgenerated.h` output.
5. **What this example deliberately does NOT cover** — bullet list with pointers.
6. **P5+ roadmap** — multi-await state machines, `MASYNC` successor decisions, etc.

## Constraints

- Allman braces, PascalCase locals, `MakeShared<T>`, `TVector`/`TMap` aliases — `Docs/CodingStyle.md`.
- No `MFUNCTION(ServerCall)` / `MFUNCTION(ClientCall)` / `MFUNCTION(RPC)` markers — example is transport-free.
- No `MAwait` / `MAwaitOk` / `TPlayerCommandFuture` / `MASYNC` / `MFUTURE` references — P4 retired all of these.
- No `Co-Authored-By` / AI attribution in commits (`feedback_no_ai_attribution_in_commits.md`).
- The example must run **without** a service registry, gateway, or EchoService — `mession_reflection_codegen` is the only "magic."

## File budget

- 4 new files: `AsyncDemo.h`, `AsyncDemo.cpp`, `Tests/main.cpp`, `Docs/superpowers/examples/2026-07-29-cpp17-async-demo.md`
- 2 modified: `CMakeLists.txt` (target + codegen dep + generated-group attach)
- Total: 4 created + 2 modified = 6 files — well within any reasonable cap.

## Open questions

None blocking. The Example's scope is tight by design; clarifying questions belong in the plan/implementation phase, not here.

---

**Next step:** writing-plans skill produces `Docs/superpowers/plans/2026-07-29-async-demo-example.md` with a task breakdown for subagent-driven-development.