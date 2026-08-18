# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
# Configure and build
cmake -S . -B Build -DCMAKE_BUILD_TYPE=Release
cmake --build Build -j4

# Windows
Scripts\Build.bat Release

# Start PoC topology (Registry → 2×EchoService → Gateway)
python3 Scripts/servers.py start --build-dir Build

# Stop servers
python3 Scripts/servers.py stop --build-dir Build

# Run validation (client chains through Gateway)
python3 Scripts/validate.py --build-dir Build --no-build

# Run specific validation suite(s)
python3 Scripts/validate.py --build-dir Build --no-build --suite player_state
python3 Scripts/validate.py --build-dir Build --no-build --list-suites

# Protocol / reflection checks after RPC or MHeaderTool changes
python3 Scripts/verify_protocol.py

# Log module unit tests (after Log-related changes)
cmake --build Build --target LogTest -j4 && ./Bin/LogTest

# Style toolchain (C1 landed; bulk format C2–C5 still TODO — see Docs/CodingStyle.md)
bash Scripts/check-style.sh
```

## PoC Topology (current truth)

Historical six-server layout (Login/World/Scene/Router/Mgo) was collapsed for PoC. **Do not restore those services.** Business surface is EchoService instances.

| Process | Port (default) | Role |
|---------|----------------|------|
| MServiceRegistry | 18000 | Service discovery: register / heartbeat / endpoint push |
| EchoService (×N) | 7001, 7002, … | Homogeneous business workers; own ActorIds via `--inst` / `--actors` |
| GatewayServer | 8001 | Sole client-facing entry; routes ClientCall to backends |

All business processes take `--registry=host:port`. Peers are **not** static CLI lists; they come from Registry + `MEndpointCache`.

```
UE / Scripts/validate.py
        │  MT_FunctionCall (FunctionId path)
        ▼
  Gateway :8001  ──MEndpointCache──►  EchoService A/B :7001/:7002
        │                                    ▲
        └────────── --registry ──────────────┤
                                             ▼
                                   MServiceRegistry :18000
```

## Client / server request flow

1. Client connects to **Gateway** and sends `MT_FunctionCall` (stable **FunctionId** envelope; legacy `EClientMessageType` / `ForwardedClientCall` removed).
2. Gateway looks up `MClientManifest::FindByFunctionId` (emit still incomplete — table may be empty/stub; do not reintroduce hard-coded route tables as the long-term design).
3. Gateway resolves backend via **`MEndpointCache::GetOrConnect(EServerType)`** (fed by Registry), then server RPC.
4. EchoService handles `MFUNCTION(ServerCall)` on local actors (`MActorRouter`); cross-instance uses `MRpcChannel::CallToActor` + cache.
5. Downlink to clients is Gateway-mediated (`PushClientDownlink` / `SendToClient` style paths). Server→client push targeting framework: `MClientTargetResolver` + `MClientTargetContextGuard` (framework landed; full call-site wiring still ongoing).

## Object / actor model

- No restored Player object tree. Addressing is **flat ActorId (uint64)** → process / connection.
- `MActorRouter` — process-local actor registration and lookup; transport resolution goes through `MEndpointCache` where remote.
- Properties still use `MPROPERTY(...)` with domain tags when applicable:
  - `PersistentData` — survives across sessions (persistence not in PoC hot path)
  - `Replicated` — synchronized to clients
  - `PersistentData | Replicated` — both

## Core runtime layers

| Layer | Location | Notes |
|-------|----------|--------|
| Core I/O / loops | `Source/Common/IO`, `Runtime/EventLoop`, `Runtime/Concurrency` | sockets, net/task loops, fibers (Windows fiber backend still limited) |
| Log | `Source/Common/Runtime/Log` | **MLog** async pipeline (ring + dispatcher + sinks). Use `LOG_*` / `CORE_LOG` / `NET_LOG` etc. from `Log.h`. Legacy `Logger.h` removed. |
| RPC | `Source/Common/Net/Rpc` | `MRpcChannel`, FunctionId dispatch, transport |
| Service discovery | `Source/Common/Net/ServiceDiscovery` | `Endpoint*`, `RegistryProtocol`, `MEndpointCache` |
| Client target | `Source/Common/Net/ClientCall` | `MClientTargetResolver` |
| Servers | `Source/Servers/{App,Gateway,EchoService,ServiceRegistry}` | `MService` / `MServiceMain` config + lifecycle |
| Reflection tool | `Source/Tools/MHeaderTool` | generates into `Build/Generated/` |

## Core conventions

### Naming
- `S*` structs, `M*` classes, `E*` enums, `I*` interfaces
- `bXxx` booleans, `InXxx` / `OutXxx` parameters
- PascalCase for functions, types, locals

### Style
- Full rules: **`Docs/CodingStyle.md`** (ColumnLimit 240, Allman braces, etc.)
- Tooling: `.clang-format`, `Scripts/check-style.sh` (includes reflected-ABI guard), optional pre-commit via `Scripts/install-hooks.sh`
- **C1 (docs + tools) is on main; C2–C5 bulk reformat is TODO** — avoid huge format-only PRs mixed with features

### STL wrapping
Prefer project aliases (`TVector`, `TMap`, `TSharedPtr`, …) over raw STL. Add new aliases in `Source/Common/Runtime/MLib.h` first.

### Shared pointers
Use `MakeShared<T>(...)`, not `TSharedPtr(new T(...))`. Interface assign: `TSharedPtr<IBase> Ptr = MakeShared<MImpl>(...)`.

### Control flow
Always braces for `if` / `for` / `while`, even single statements.

### Protocol structures
Prefer `MSTRUCT` + `MPROPERTY` over hand-rolled serialization.

## Reflection system

`MHeaderTool` generates glue under `Build/Generated/` from:

- `MCLASS` / `MSTRUCT` — type registration
- `MPROPERTY(...)` — fields + domain / CLI meta
- `MFUNCTION(...)` — RPC / client surface (`ServerCall`, `CallClient`, `Client`, `Async`, …)
- `MGENERATED_BODY` — boilerplate

After changing reflection macros or reflected types: rebuild so `Build/Generated/` updates, then `Scripts/verify_protocol.py` when protocol-related.

ClientCall stable IDs default from function identity; use `Api=...` / `ClientApi=...` when renames must keep wire IDs.

## C++17 async model — quick reference

- **Contract**: every async function returns `SFutureResult<T>` (no exceptions on the result path; `Get()` throws `FFutureResultError` on err). The legacy `MFUTURE(T)` macro is gone.
- **Mark async methods**: `MFUNCTION(..., Async)` (class members) or `MFUNCTION(Async)` (free functions). `MASYNC` was considered in P0–P3 and dropped in P4 — do not reintroduce.
- **Await**: `AWAIT_OK(expr)` — only inside an `Async` function; the macro expands to `Frame->AwaitOk(expr)` so a local `Frame` must exist in scope.
- **Sync barrier**: `F.Get()` / `F.Wait()` outside Async functions. Never on the event-loop thread for a future that depends on that loop (spec §8.2 redline — `MAsyncContext::IsSameContext` triggers assert in DEBUG).
- **Fiber removed**: `MAwait` / `MAwaitOk` / `TPlayerCommandFuture` deleted in P4; the leftover fiber plumbing (`FiberAwait` / `FiberScheduler` / `CommandExecutionContext` / `MEventAwait`) had no consumers and was removed.
- **`.Async.cpp` convention**: async business bodies live in `Xxx.Async.cpp` (codegen-only source, zero `#ifdef`; MHeaderTool scans it automatically, business build skips it). See spec §7.2.1.
- Full design: `Docs/superpowers/specs/2026-07-24-cpp17-async-await.md`; P4 wrap (free-func codegen, deletions): `Docs/superpowers/specs/2026-07-28-async-p4-wrap.md`.

## Recommended reading

1. `Docs/RefactorArchitectureAndRpc.md` — original refactor narrative (**partially outdated**: World middle-tier / six-server diagrams; still useful for Actor/RPC intent)
2. `Docs/superpowers/specs/2026-07-13-service-registry-design.md` — Registry + EndpointCache
3. `Docs/superpowers/specs/2026-07-14-log-module-design.md` — log design
4. `Docs/CodingStyle.md` — coding style + C2–C5 backlog
5. `Docs/superpowers/specs/2026-07-07-actor-rpc-refactor.md` — actor RPC refactor context
6. `Docs/superpowers/specs/2026-07-24-cpp17-async-await.md` — **C++17 async model** (`SFutureResult`, `Async`/`AWAIT`, `MAsyncContext`; not C++20 coroutines / not fiber-first)

If docs disagree with this file on **topology or process list**, trust **this file + `Scripts/servers.py`**.

## Validation strategy

After protocol / Gateway / Echo / Registry changes:

1. `cmake --build Build -j4`
2. `python3 Scripts/verify_protocol.py` (when reflection/protocol touched)
3. `python3 Scripts/validate.py --build-dir Build --no-build`

After Log changes: `./Bin/LogTest` (and optional `Scripts/validate_log_*.py`).

**Note:** full client chain validate has been flaky/timeout on recent main; treat failures as real until proven environmental. Do not assume green without running it.

## Key constraints

- Business logic lives in service/object handlers (e.g. EchoService), **not** in thin `Server` connection glue beyond routing/lifecycle
- `Server` classes: connection boundaries, registry bind, event loop — not gameplay rules
- **No direct server→client TCP** for gameplay notify; go through Gateway downlink paths
- Prefer `MEndpointCache` over hard-coded peer lists
- ClientCall stable IDs: preserve with `Api`/`ClientApi` across renames
- Do not revive deleted six-server business trees or static `--peers` as the discovery model

## Active gaps (do not “fix” by reverting architecture)

| Gap | Intent |
|-----|--------|
| `MClientManifest` real emit from `MFUNCTION(Client/CallClient/…)` | Replace stub; Gateway routes only via generated table |
| Wire `MClientTargetResolver` at session online/offline + CallClient sites | Complete server→client push path |
| Prove cross-Echo `CallToActor` under Registry actor metadata | Distributed baseline |
| Green `validate.py` chains | Gate for further protocol work |
| Coding-style C2–C5 bulk format | Deferred; see `Docs/CodingStyle.md` §落地进度 |
