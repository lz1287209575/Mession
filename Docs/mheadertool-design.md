# MHeaderTool Design Draft

## Goal

`MHeaderTool` is a lightweight code generation tool for the custom Mession reflection system.

The first milestone is to remove manual reflection glue such as:

- `RegisterAllProperties`
- `RegisterAllFunctions`
- service RPC handler storage
- `SetHandler_*`
- `GetFunctionId_*`

The target developer experience is close to Unreal Header Tool:

```cpp
class MLoginService : public MReflectObject
{
public:
    MGENERATED_BODY(MLoginService, MReflectObject, 0)

    MFUNCTION(NetServer, Rpc=ServerToServer, Reliable=true, Handler=true)
    void Rpc_OnSessionValidateRequest(uint64 ConnectionId, uint64 PlayerId, uint32 SessionKey);
};
```

Generated code should then provide the reflection registration and RPC helper glue automatically.

## Non-Goals For V1

The first version should not attempt to parse arbitrary C++.

Not supported in V1:

- overloaded reflected functions
- function templates
- class templates
- macro-generated function names
- complex declarators around reflected members
- deep inheritance analysis across unrelated translation units
- full C++ AST correctness

V1 should prefer a narrow syntax contract over broad language coverage.

## Scope

V1 only needs to support:

- classes with `MGENERATED_BODY(...)`
- properties marked by `MPROPERTY(...)`
- functions marked by `MFUNCTION(...)`
- RPC metadata extraction from `MFUNCTION(...)`
- generation of reflection registration code
- generation of service RPC handler helper APIs

## Input Conventions

The tool scans project headers under `Source/`.

V1 syntax rules:

1. `MGENERATED_BODY(...)` must appear inside a class definition.
2. `MPROPERTY(...)` must be immediately followed by a member field declaration.
3. `MFUNCTION(...)` must be immediately followed by a member function declaration.
4. Reflected function declarations must end with `;`.
5. No overloaded reflected methods.

Example:

```cpp
class MGatewayService : public MReflectObject
{
public:
    MGENERATED_BODY(MGatewayService, MReflectObject, 0)

    MFUNCTION(NetServer, Rpc=ServerToServer, Reliable=true, Handler=true)
    void Rpc_OnPlayerLoginResponse(uint64 ConnectionId, uint64 PlayerId, uint32 SessionKey);
};
```

## Tool Pipeline

The tool pipeline is intentionally simple:

1. Discover candidate headers under `Source/`.
2. Tokenize file contents.
3. Parse only the subset needed for reflected classes.
4. Build an intermediate model.
5. Emit generated `.mgenerated.h` and `.mgenerated.cpp` files.

## Intermediate Model

Suggested core data model:

```cpp
struct ParsedParameter
{
    std::string Type;
    std::string Name;
};

struct ParsedProperty
{
    std::string Name;
    std::string Type;
    std::string MacroArgs;
};

struct ParsedFunction
{
    std::string Name;
    std::string ReturnType;
    std::vector<ParsedParameter> Parameters;
    std::string MacroArgs;
    bool IsConst = false;
    bool IsRpc = false;
    bool NeedsHandler = false;
};

struct ParsedClass
{
    std::string Name;
    std::string ParentName;
    std::string FlagsExpr;
    std::string HeaderPath;
    std::vector<ParsedProperty> Properties;
    std::vector<ParsedFunction> Functions;
};
```

## Output Files

Recommended generated files per reflected header:

- `Foo.mgenerated.h`
- `Foo.mgenerated.cpp`

`Foo.mgenerated.h`:

- generated declarations that must be visible to the owning class
- optional helper declarations

`Foo.mgenerated.cpp`:

- `RegisterAllProperties`
- `RegisterAllFunctions`
- generated RPC helper storage
- generated `SetHandler_*`
- generated `GetFunctionId_*`

## Integration Pattern

Headers opt in by including their generated header:

```cpp
#include "Foo.mgenerated.h"
```

The generated source file should be compiled as part of the build.

## Metadata Strategy

V1 should normalize `MFUNCTION(...)` arguments into a small key-value model.

Recommended shape:

```cpp
MFUNCTION(NetServer, Rpc=ServerToServer, Reliable=true, Handler=true)
```

Supported keys in V1:

- `Rpc`
- `Reliable`
- `Handler`
- `Endpoint`

This makes the parser simpler and future-proof.

## Generated Rules

For each reflected function:

- generate a reflection registration entry
- if RPC metadata exists, generate `CreateRpcFunction(...)` registration
- if `Handler=true`, generate:
  - `using FHandler_<MethodName>`
  - `SetHandler_<MethodName>`
  - `GetFunctionId_<MethodName>`
  - static handler storage

For each reflected property:

- map C++ type to `EPropertyType`
- emit `MREGISTER_PROPERTY(...)`

## Build Integration

Planned CMake flow:

1. Build `MHeaderTool`.
2. Run it before normal targets.
3. Generate outputs into a derived directory such as:
   - `${CMAKE_BINARY_DIR}/Generated`
4. Add generated sources to relevant targets.

Recommended first integration path:

- keep `MHeaderTool` as a standalone executable target
- add an opt-in generation target first
- once stable, make game/server targets depend on it

## Error Reporting

The tool must fail with actionable diagnostics:

- file path
- line number
- class name when known
- concise reason

Examples:

- `unsupported overloaded MFUNCTION`
- `MFUNCTION must be followed by a member function declaration`
- `missing MGENERATED_BODY inside reflected class`

## Milestones

### Milestone 1

- create tool executable
- header discovery
- tokenizer
- reflected class discovery

### Milestone 2

- parse `MFUNCTION(...)`
- emit function registration code
- support service RPC glue generation

### Milestone 3

- parse `MPROPERTY(...)`
- emit property registration

### Milestone 4

- integrate generated files into main build
- migrate existing handwritten registration out of source files

## Recommended Next Step

Start with a tool skeleton that:

- accepts `--source-root`, `--output-dir`
- discovers headers
- reports candidate reflected classes
- writes placeholder generated files

Once that path is stable, add real parsing and code emission incrementally.
