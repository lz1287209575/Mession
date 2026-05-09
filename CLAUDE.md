# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
# Configure and build
cmake -S . -B Build -DCMAKE_BUILD_TYPE=Release
cmake --build Build -j4

# Windows
Scripts\Build.bat Release

# Start servers locally
python3 Scripts/servers.py start --build-dir Build

# Stop servers
python3 Scripts/servers.py stop --build-dir Build

# Run validation suite
python3 Scripts/validate.py --build-dir Build --no-build

# Run specific validation suite
python3 Scripts/validate.py --build-dir Build --no-build --suite player_state
python3 Scripts/validate.py --build-dir Build --no-build --suite scene_downlink
python3 Scripts/validate.py --build-dir Build --no-build --suite combat_commit

# List available validation suites
python3 Scripts/validate.py --build-dir Build --no-build --list-suites
```

## Architecture Overview

### Six-Server Topology

| Server | Port | Purpose |
|--------|------|---------|
| GatewayServer | 8001 | Client entry, routes ClientCall via MClientManifest |
| LoginServer | 8002 | Session issuance and validation |
| WorldServer | 8003 | Player object tree ownership, orchestration |
| SceneServer | 8004 | Scene entry/exit, sync, lightweight combat runtime |
| RouterServer | 8005 | Player routing registration and lookup |
| MgoServer | 8006 | Player persistence (in-memory or MongoDB) |

### Client Request Flow

1. Client connects to Gateway → sends `MT_FunctionCall`
2. Gateway looks up target via `MClientManifest` → forwards to business server
3. WorldServer receives via `MWorldClient::Client_*` → dispatches to `MPlayerService`
4. `MPlayerService` finds `MPlayer` and executes on child objects via `MFUNCTION(ServerCall)`

### Object State Model

Player state is a tree rooted at `MPlayer`:
- `MPlayerSession` / `MPlayerController` / `MPlayerPawn`
- `MPlayerProfile` / `MPlayerInventory` / `MPlayerProgression` / `MPlayerCombatProfile`

Properties use `MPROPERTY(...)` with domain tags:
- `PersistentData` — survives across sessions via MgoServer
- `Replicated` — synchronized to clients
- `PersistentData | Replicated` — both

### Three-Layer World Dispatch

1. **WorldClient** (`MWorldClient`) — thin client-call adapter, projects to PlayerService
2. **PlayerService** (`MPlayerService`) — Runtime dispatch contract, validation, barrier coordination
3. **ObjectCall** (`MObjectCall*`) — local/remote object invocation routing

## Core Conventions

### Naming
- `S*` prefix for structs, `M*` for classes, `E*` for enums
- `bXxx` for booleans, `InXxx`/`OutXxx` for function parameters
- PascalCase for functions, types, local variables

### STL Wrapping Rule
Prefer project aliases (`TVector`, `TMap`, `TSharedPtr`, etc.) over raw STL. Add new aliases to `Source/Common/Runtime/MLib.h` first.

### Shared Pointer Construction
Use `MakeShared<T>(...)` instead of `TSharedPtr(new T(...))`. For interface assignments: `TSharedPtr<IBase> Ptr = MakeShared<MImpl>(...)`.

### Control Flow
Always use braces for `if`/`for`/`while`, even single statements.

### Protocol Structures
Prefer `MSTRUCT + MPROPERTY` over manual serialization.

## Reflection System

`MHeaderTool` generates glue code to `Build/Generated/` from macros:
- `MCLASS` / `MSTRUCT` — type registration
- `MPROPERTY(...)` — field with domain tags and validation metadata
- `MFUNCTION(...)` — RPC function with ServerCall/ClientCall semantics
- `MGENERATED_BODY` — generated boilerplate

If you modify reflection macros or add new reflected types, verify `Build/Generated/` updates correctly.

## Recommended Reading

1. `Docs/Architecture.md` — system layers, server responsibilities, object model
2. `Docs/BuildAndRun.md` — build, startup, validation commands
3. `Docs/RuntimeAndRpc.md` — reflection, RPC dispatch, async patterns
4. `Docs/Validation.md` — testing strategy and regression suites
5. `Docs/PlayerRpcDevelopment.md` — adding new Player RPC functions

## Validation Strategy

After protocol changes:
1. Build (`cmake --build Build -j4`)
2. Run `Scripts/verify_protocol.py`
3. Run `Scripts/validate.py --build-dir Build --no-build`

After Player object tree changes, check: login initial state, write→query consistency, logout→relogin recovery.

## Key Constraints

- Business logic belongs in `MPlayerService` or explicit `Workflow`, not in `Server` classes
- `Server` handles connection boundaries and runtime context, not business details
- Player business lives in `MPlayer` child objects, not World endpoints
- ClientCall stable IDs use function name by default; use `Api=...` to preserve IDs during refactoring
