/**
 * @file AsyncDemo.h
 * @brief AsyncDemo - end-to-end C++17 async/await example declarations.
 *
 * Two namespace-scope free async functions (no transport tags) — see
 * Docs/superpowers/specs/2026-07-29-async-demo-example-design.md and
 * Docs/superpowers/examples/2026-07-29-cpp17-async-demo.md.
 */
#pragma once

#include "Common/Runtime/Async/MAsync.h"
#include "Common/Runtime/Async/AwaitMacros.h"

// AsyncDemo_FreeAsyncFrames.mgenerated.h is produced by MHeaderTool's P4
// free-function pass from the MFUNCTION(Async) markers below. It declares
// MHeaderTool_AsyncFrame_Free_ComputeAsync and MHeaderTool_AsyncFrame_Free_ChainAsync.
// Build order: AsyncDemo.cpp must include this file before the AWAIT_OK
// macro expansion in either function body can resolve the local `Frame`.
#include "AsyncDemo_FreeAsyncFrames.mgenerated.h"

namespace AsyncDemoNS
{

// Inner async — simulates an I/O-style future (here: synchronously ready).
// P4 v1 limitation: the Frame is a pass-through AwaitOk; no state machine.
MFUNCTION(Async)
SFutureResult<int> ComputeAsync(int Seed);

// Outer async — chains the inner one via AWAIT_OK, returns a derived value.
MFUNCTION(Async)
SFutureResult<int> ChainAsync(int Seed);

} // namespace AsyncDemoNS
