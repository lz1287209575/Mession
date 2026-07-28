# C++17 Async / Await — P4 收口 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend `MFUNCTION(Async)` to namespace-scope free functions (replacing the spec §6.2 `MASYNC` placeholder), delete the legacy `MAwait` / `MAwaitOk` / `TPlayerCommandFuture` Fiber API, sync the parent async spec from "deprecated" to "removed", and add a CLAUDE.md async quick reference — single PR, no runtime behavior change.

**Architecture:** P4 is a MHeaderTool + docs consolidation. The tool grows a new `ProcessFreeFunctions` pass (parallel to the existing class-method pass) that emits `<Header>_FreeAsyncFrames.mgenerated.h` with one `MHeaderTool_AsyncFrame_Free_<Func>` struct per free `MFUNCTION(Async)`; the legacy Fiber API surface is deleted in one atomic commit; the parent spec gets three doc-touch updates; `CLAUDE.md` gets a 5-10 line async cheat sheet. No changes to `MFiberScheduler.h`, dispatch pending path, or runtime behavior.

**Tech Stack:** C++17, MHeaderTool (header-based, no .cpp split), in-tree `CodeGenerator` and `FunctionParser` with `ExtractResponseType` / `ApplyFunctionMetadataFromMacroArgs` / `MakeMaskedCopy` / `FindMatching` / `SplitTopLevelArgs` helpers already in place. Tests use the in-house `EXPECT_TRUE` / `RUN_TESTS()` pattern (`Source/Common/Runtime/Log/Tests/TestHarness.h`).

## Global Constraints

- Language standard: C++17 (CMakeLists.txt:6 + :210 already set)
- Naming: `M*` classes, `S*` structs, `bXxx` bool, no underscore
- Style: ColumnLimit 240, Allman braces
- Shared pointers: `MakeShared<T>(...)` only
- Single response contract: `SFutureResult<T>`
- Reflection: `MPROPERTY` field names are public ABI (no rename/reorder)
- Commit messages: no AI attribution / Co-Authored-By lines
- Test target naming: P3 has `AsyncFrameTest` in `Source/Servers/EchoService/Tests/main.cpp` — follow same pattern for new test
- Single PR scope: ≤12 files

---

## File Structure

### Created
| File | Concern |
|------|---------|
| `/root/Mession/Source/Tools/MHeaderTool/Tests/FreeAsyncFuncGenTest.cpp` | New TDD test for the free-function Frame generator (Task 4) |
| `/root/Mession/Docs/superpowers/plans/2026-07-28-async-p4-wrap.md` | This plan |

### Modified
| File | Concern |
|------|---------|
| `/root/Mession/Source/Tools/MHeaderTool/Core/Types.h` | Add `SFreeAsyncFunc` struct (Task 1) |
| `/root/Mession/Source/Tools/MHeaderTool/MHeaderTool.cpp` | Wire `ProcessFreeFunctions` + `EmitFreeAsyncFramesHeader` write-out (Task 2 + 3) |
| `/root/Mession/Source/Tools/MHeaderTool/Generation/CodeGenerator.h` | Add `EmitFreeAsyncFramesHeader` declaration + impl (Task 3) |
| `/root/Mession/Source/Common/Runtime/Concurrency/FiberAwait.h` | Delete `MAwait` / `MAwaitOk` overloads + `TPlayerCommandFuture` alias (Task 5) |
| `/root/Mession/Source/Common/Net/Rpc/MRpcChannel.h` | Rewrite doxygen line 26 example (Task 6) |
| `/root/Mession/Source/Common/Runtime/Async/MAsync.h` | Rewrite line 237 comment (Task 6) |
| `/root/Mession/Docs/superpowers/specs/2026-07-24-cpp17-async-await.md` | Sync §2 / §6.2 / §7.4 / §14 / §16.8 / §17 Q4 (Task 7) |
| `/root/Mession/CLAUDE.md` | Add async quick-ref section (Task 8) |
| `/root/Mession/CMakeLists.txt` | Register `FreeAsyncFuncGenTest` target (Task 4) |

Total: 2 created + 9 modified = 11 files (within ≤12 limit).

---

## Tasks

### Task 1: Add `SFreeAsyncFunc` to MHeaderTool Core/Types.h

**Files:**
- Modify: `/root/Mession/Source/Tools/MHeaderTool/Core/Types.h:97-117` (insert new struct after `SParsedClass`)

**Interfaces:**
- Consumes: nothing (no prior task)
- Produces: `MHeaderTool::SFreeAsyncFunc` struct with fields `HeaderPath`, `Name`, `ResponseType`, `AsyncBody` — consumed by Task 3 generator and Task 4 test

- [ ] **Step 1: Add the struct definition**

Insert after the closing `};` of `SParsedClass` (line 117), before the `// =========================================================================` block at line 119:

```cpp
// ============================================================================
// Free Async Function (P4 — spec 2026-07-28 §B)
// ============================================================================
//
// Captures a namespace-scope free function declared with `MFUNCTION(Async)`.
// Unlike `SParsedFunction` (which lives inside a class), this struct has no
// owning class — only the header path + function identity. Consumed by
// `CodeGenerator::EmitFreeAsyncFramesHeader` to emit one Frame struct per
// free async function into `<Header>_FreeAsyncFrames.mgenerated.h`.

struct SFreeAsyncFunc
{
    fs::path HeaderPath;
    std::string Name;
    std::string ResponseType;
    std::string AsyncBody;
};
```

- [ ] **Step 2: Verify the header compiles**

Run: `cmake --build /root/Mession/Build --target MHeaderTool -j4`
Expected: build succeeds; `SFreeAsyncFunc` is now visible to `MHeaderTool.cpp` and `CodeGenerator.h`.

- [ ] **Step 3: Commit**

```bash
git add Source/Tools/MHeaderTool/Core/Types.h
git commit -m "refactor(mht): add SFreeAsyncFunc type for free MFUNCTION(Async) codegen"
```

---

### Task 2: Add `ProcessFreeFunctions` pass + reject transport tags

**Files:**
- Modify: `/root/Mession/Source/Tools/MHeaderTool/MHeaderTool.cpp` (insert `ProcessFreeFunctions` near the top-level `main` flow; add a new `TVector<SFreeAsyncFunc>` accumulator)
- Modify: `/root/Mession/Source/Tools/MHeaderTool/Parsing/FunctionParser.h:265-312` (re-use `ParseFunctionDeclaration` and `ApplyFunctionMetadataFromMacroArgs` as-is — they accept any declaration + macro args string)

**Interfaces:**
- Consumes: `SFreeAsyncFunc` from Task 1
- Produces: `ProcessFreeFunctions(const std::map<fs::path, std::string>& FileContents) -> std::vector<SFreeAsyncFunc>` — called by `main()` after the existing class-pass loop; populates the per-header free-function list. Throws `std::runtime_error` with spec citation when a free function uses `MFUNCTION(ServerCall, Async)` (or any other transport tag).

- [ ] **Step 1: Implement `ProcessFreeFunctions` in MHeaderTool.cpp**

Insert above the `int main` (after the closing `}  // namespace` at line 132):

```cpp
namespace MHT = MHeaderTool;

// P4 — spec 2026-07-28 §B: scan every header in `FileContents` for
// namespace-scope `MFUNCTION(Async)` free functions. Reject any free
// function that carries a transport tag (ServerCall/ClientCall/RPC/
// NetServer/NetClient/Client) — those belong on class methods, not on
// free functions. The error message cites the relevant specs so the
// user can navigate to the rule.
std::vector<MHT::SFreeAsyncFunc> ProcessFreeFunctions(
    const std::map<fs::path, std::string>& FileContents)
{
    std::vector<MHT::SFreeAsyncFunc> Result;

    // Transport tags that must NOT appear on a free function (spec 2026-07-24 §6.1
    // + 2026-07-28 §B — transport is a class-method concept).
    static const std::set<std::string> kTransportTags = {
        "ServerCall", "ClientCall", "RPC", "NetServer", "NetClient", "Client"
    };

    for (const auto& [headerPath, contents] : FileContents)
    {
        // Skip headers that have no reflection markers — mirrors main()'s
        // `HeaderScanner::HasReflectionMarkers` short-circuit.
        if (contents.find("MFUNCTION") == std::string::npos)
        {
            continue;
        }

        // Find every "MFUNCTION(" outside of any class body. v1 simplification:
        // we scan top-level only — class bodies are stripped by a quick
        // brace-mask. Cross-file free functions are out of scope (spec §6
        // risk register).
        std::string Masked = contents;
        int Depth = 0;
        for (size_t i = 0; i < Masked.size(); ++i)
        {
            char ch = Masked[i];
            if (ch == '{') ++Depth;
            else if (ch == '}')
            {
                if (Depth > 0) --Depth;
            }
            else if (Depth > 0)
            {
                Masked[i] = ' ';
            }
        }

        const std::string Needle = "MFUNCTION(";
        size_t SearchPos = 0;
        while ((SearchPos = Masked.find(Needle, SearchPos)) != std::string::npos)
        {
            const size_t MacroOpen = contents.find('(', SearchPos);
            const size_t MacroClose = (MacroOpen == std::string::npos)
                ? std::string::npos
                : MHT::FindMatching(contents, MacroOpen, '(', ')');
            if (MacroOpen == std::string::npos || MacroClose == std::string::npos)
            {
                SearchPos = SearchPos + Needle.size();
                continue;
            }

            const std::string MacroArgs =
                contents.substr(MacroOpen + 1, MacroClose - MacroOpen - 1);

            // Reject transport tags.
            for (const auto& tag : kTransportTags)
            {
                if (MacroArgs.find(tag) != std::string::npos)
                {
                    throw std::runtime_error(
                        "MHeaderTool: free function at " + headerPath.string() +
                        " carries transport tag '" + tag +
                        "' in MFUNCTION — spec 2026-07-24 §6.1 + 2026-07-28 §B "
                        "require transport on class methods only; "
                        "use plain `MFUNCTION(Async)` for free functions");
                }
            }

            // Only handle MFUNCTION(Async) — i.e. macroArgs == "Async"
            // after Trim. Anything else is irrelevant to free-function codegen.
            if (MHT::Trim(MacroArgs) != "Async")
            {
                SearchPos = MacroClose + 1;
                continue;
            }

            // Parse the declaration that follows the macro: walk past
            // whitespace + return-type, find the '(' of the signature, then
            // the function name (last identifier before '('), and extract
            // the response type.
            size_t DeclStart = MacroClose + 1;
            while (DeclStart < contents.size() &&
                   std::isspace(static_cast<unsigned char>(contents[DeclStart])))
            {
                ++DeclStart;
            }
            const size_t SigOpen = contents.find('(', DeclStart);
            if (SigOpen == std::string::npos)
            {
                SearchPos = MacroClose + 1;
                continue;
            }
            const size_t SigClose = MHT::FindMatching(contents, SigOpen, '(', ')');
            if (SigClose == std::string::npos)
            {
                SearchPos = MacroClose + 1;
                continue;
            }
            // Walk backward from SigOpen over the return type to find the
            // identifier that is the function name (last token before '(').
            size_t NameEnd = SigOpen;
            while (NameEnd > DeclStart &&
                   std::isspace(static_cast<unsigned char>(contents[NameEnd - 1])))
            {
                --NameEnd;
            }
            size_t NameStart = NameEnd;
            while (NameStart > DeclStart &&
                   (std::isalnum(static_cast<unsigned char>(contents[NameStart - 1])) ||
                    contents[NameStart - 1] == '_'))
            {
                --NameStart;
            }
            if (NameStart == NameEnd)
            {
                SearchPos = MacroClose + 1;
                continue;
            }
            const std::string FuncName = contents.substr(NameStart, NameEnd - NameStart);

            // Return type = everything from DeclStart up to the start of FuncName.
            const std::string ReturnType =
                MHT::Trim(contents.substr(DeclStart, NameStart - DeclStart));

            // Response type = unwrap SFutureResult<T> to T. Reuse the
            // CodeGenerator helper — declared in CodeGenerator.h, defined
            // out-of-line below in Task 3 Step 2. For Task 2 we only need
            // a string-stripping version local to this TU.
            std::string ResponseType = ReturnType;
            const std::string SfPrefix = "SFutureResult<";
            if (ResponseType.rfind(SfPrefix, 0) == 0 && ResponseType.back() == '>')
            {
                ResponseType = ResponseType.substr(
                    SfPrefix.size(), ResponseType.size() - SfPrefix.size() - 1);
            }

            MHT::SFreeAsyncFunc Func;
            Func.HeaderPath = headerPath;
            Func.Name = FuncName;
            Func.ResponseType = MHT::Trim(ResponseType);
            Func.AsyncBody = "";  // free functions are declaration-only in P4
            Result.push_back(std::move(Func));

            SearchPos = MacroClose + 1;
        }
    }

    return Result;
}
```

- [ ] **Step 2: Verify the helper compiles**

Run: `cmake --build /root/Mession/Build --target MHeaderTool -j4`
Expected: clean build (helper is declared, not yet called from main; no behavior change).

- [ ] **Step 3: Commit**

```bash
git add Source/Tools/MHeaderTool/MHeaderTool.cpp Source/Tools/MHeaderTool/Tests/FreeAsyncFuncGenTest.cpp
git commit -m "refactor(mht): ProcessFreeFunctions scan + transport-tag rejection (P4)"
```

---

### Task 3: Emit `<Header>_FreeAsyncFrames.mgenerated.h`

**Files:**
- Modify: `/root/Mession/Source/Tools/MHeaderTool/Generation/CodeGenerator.h` (add `EmitFreeAsyncFramesHeader` after the existing `EmitAsyncFramesHeader` at line 154)
- Modify: `/root/Mession/Source/Tools/MHeaderTool/MHeaderTool.cpp` (after line 388, write out one file per header that has free async functions)

**Interfaces:**
- Consumes: `std::vector<SFreeAsyncFunc>` from Task 2; `MHeaderTool::SanitizeIdentifier`, `MHeaderTool::ExtractResponseType` already in `CodeGenerator.h`
- Produces: `CodeGenerator::EmitFreeAsyncFramesHeader(const std::vector<SFreeAsyncFunc>& Funcs, const fs::path& HeaderPath) const -> std::string` — returns empty string when `Funcs` is empty (caller skips file write). For each function, emits one `MHeaderTool_AsyncFrame_Free_<FuncName>` struct with `AwaitOk` pass-through, `StoredValue`, `AwaitedSlot` (mirrors class-method Frame shape so `AWAIT_OK` macro works unchanged).

- [ ] **Step 1: Add `EmitFreeAsyncFramesHeader` declaration + impl to CodeGenerator.h**

Insert after the closing `}` of `EmitAsyncFramesHeader` (line 154), before the `private:` line:

```cpp
public:
    // P4 — spec 2026-07-28 §B: emit a `<Header>_FreeAsyncFrames.mgenerated.h`
    // file containing one `MHeaderTool_AsyncFrame_Free_<FuncName>` struct per
    // free `MFUNCTION(Async)` function in `Funcs`. Naming mirrors the
    // class-method `<Class>_AsyncFrames.h` so the AWAIT_OK macro contract
    // (AWAIT_OK expands to `Frame->AwaitOk(expr)`) works for both.
    //
    // Returns empty string if `Funcs` is empty so the caller can skip
    // the file write. Per spec Q1, the filename suffix is `FreeAsyncFrames`
    // to align with the class-method `AsyncFrames` pattern.
    std::string EmitFreeAsyncFramesHeader(
        const std::vector<SFreeAsyncFunc>& Funcs,
        const fs::path& HeaderPath) const
    {
        if (Funcs.empty()) return {};

        std::ostringstream out;
        out << "#pragma once\n";
        out << "// Generated by MHeaderTool\n";
        out << "// Source: " << HeaderPath.string() << "\n";
        out << "// Free Async Frame struct definitions (P4 — spec 2026-07-28 §B)\n";
        out << "//\n";
        out << "// The user header must #include this file BEFORE the free\n";
        out << "// function declarations so the AWAIT_OK macro can reference\n";
        out << "// the local `Frame` symbol's `MHeaderTool_AsyncFrame_Free_<FuncName>`.\n";
        out << "\n";

        out << "#include \"Common/Runtime/Async/MAsync.h\"\n";
        out << "#include \"Common/Runtime/Reflect/Reflection.h\"\n";
        out << "\n";

        for (const auto& func : Funcs)
        {
            EmitFreeAsyncStateMachine(out, func);
        }

        return out.str();
    }

private:
    // P4 — emit one Frame struct for a free async function. Mirrors the
    // class-method Frame shape (AwaitOk pass-through + AwaitedSlot +
    // StoredValue) so the AWAIT_OK macro contract is identical for both.
    void EmitFreeAsyncStateMachine(
        std::ostringstream& out,
        const SFreeAsyncFunc& func) const
    {
        const std::string FrameName =
            "MHeaderTool_AsyncFrame_Free_" + SanitizeIdentifier(func.Name);
        const std::string RespType = func.ResponseType;

        out << "// " << FrameName
            << ": Free async frame helper (P4 — spec 2026-07-28 §B)\n";
        out << "//   User free function constructs Frame, returns AWAIT_OK(expr).\n";
        out << "//   AwaitOk is pass-through: returns Awaited verbatim so user\n";
        out << "//   body `return AWAIT_OK(expr)` matches the SF<Resp> signature.\n";
        out << "struct " << FrameName << "\n";
        out << "{\n";
        out << "    // Single-await slot (P4 v1 — same shape as class-method Frame)\n";
        out << "    SFutureResult<" << RespType << "> AwaitedSlot;\n";
        out << "    " << RespType << " StoredValue {};\n";
        out << "\n";
        out << "    // AwaitOk — pass-through entry used by the AWAIT_OK macro.\n";
        out << "    SFutureResult<" << RespType
            << "> AwaitOk(SFutureResult<" << RespType << "> Awaited)\n";
        out << "    {\n";
        out << "        AwaitedSlot = Awaited;\n";
        out << "        if (Awaited.IsReady())\n";
        out << "        {\n";
        out << "            const auto& R = Awaited.PeekResult();\n";
        out << "            if (R.IsOk()) StoredValue = R.GetValue();\n";
        out << "        }\n";
        out << "        return Awaited;\n";
        out << "    }\n";
        out << "};\n";
        out << "\n";
    }

public:
```

- [ ] **Step 2: Wire the write-out in MHeaderTool.cpp main()**

After the existing class loop (after line 388, before the `// 统计` block at line 390), insert:

```cpp
            // P4: emit one `<Header>_FreeAsyncFrames.mgenerated.h` per header
            // that contains at least one `MFUNCTION(Async)` free function.
            // ProcessFreeFunctions was called above in Step 2 of Task 2 wiring;
            // for the actual integration in main() we re-scan here to keep
            // the per-header grouping symmetric with the class pass.
            std::map<std::string, std::vector<MHT::SFreeAsyncFunc>> freeByHeader;
            {
                std::vector<MHT::SFreeAsyncFunc> AllFree = ProcessFreeFunctions(fileContents);
                for (auto& f : AllFree)
                {
                    freeByHeader[f.HeaderPath.string()].push_back(std::move(f));
                }
            }
            for (const auto& [headerStr, funcs] : freeByHeader)
            {
                std::string freeCode = codeGen.EmitFreeAsyncFramesHeader(
                    funcs, fs::path(headerStr));
                fs::path baseName = fs::path(headerStr).stem();
                fs::path freePath = options.OutputDir /
                    (MHT::SanitizeIdentifier(baseName.string()) + "_FreeAsyncFrames.mgenerated.h");
                MHT::WriteFile(freePath, freeCode);
            }

```

- [ ] **Step 3: Verify the build still passes**

Run: `cmake --build /root/Mession/Build --target MHeaderTool -j4`
Expected: clean build. No behavior change yet (no free async functions exist in tree; the file write loop produces no outputs in that case).

- [ ] **Step 4: Commit**

```bash
git add Source/Tools/MHeaderTool/Generation/CodeGenerator.h Source/Tools/MHeaderTool/MHeaderTool.cpp
git commit -m "feat(mht): emit FreeAsyncFrames header for MFUNCTION(Async) free funcs"
```

---

### Task 4: TDD test for free-function Frame generation (fail → pass)

**Files:**
- Modify: `/root/Mession/Source/Tools/MHeaderTool/Tests/FreeAsyncFuncGenTest.cpp` (fill in the placeholder from Task 2 Step 1)
- Modify: `/root/Mession/CMakeLists.txt` (add the `FreeAsyncFuncGenTest` target near the existing `AsyncFrameTest` block at line 542-564)
- Modify: `/root/Mession/Source/Tools/MHeaderTool/CMakeLists.txt` (refactor `MHeaderTool` from a one-file `add_executable` to a library + thin-executable split so the test can link the same sources without colliding with the program `main()`)

**Interfaces:**
- Consumes: `MHeaderTool::ProcessFreeFunctions`, `MHeaderTool::CodeGenerator::EmitFreeAsyncFramesHeader`, `MHeaderTool::SFreeAsyncFunc` from Tasks 1-3
- Produces: a green `FreeAsyncFuncGenTest` binary; 2 test cases (happy path + reject)

**Important — library split.** The current `MHeaderTool/CMakeLists.txt:34` builds the whole tool as one `add_executable(MHeaderTool ...)`. Including `MHeaderTool.cpp` from the test TU would duplicate `main()`. Resolve by splitting the source list out:

- [ ] **Step 1: Refactor MHeaderTool CMakeLists to library + thin executable**

Replace `/root/Mession/Source/Tools/MHeaderTool/CMakeLists.txt` with:

```cmake
# MHeaderTool - 反射代码生成工具
# 模块化重构版本

set(MHEADERTOOL_SOURCES
    # Core - 核心数据类型和接口
    Core/Types.h

    # Util - 工具函数
    Util/StringUtil.h
    Util/FileUtil.h

    # Parsing - 解析模块
    Parsing/HeaderScanner.h
    Parsing/ClassParser.h
    Parsing/FunctionParser.h
    Parsing/PropertyParser.h
    Parsing/EnumParser.h
    Parsing/MacroExpander.h

    # Cache - 增量编译缓存
    Cache/BuildCache.h
    Cache/CacheReader.h
    Cache/CacheWriter.h
    Cache/IncrementalDriver.h

    # Generation - 代码生成
    Generation/CodeGenerator.h
    Generation/ManifestGenerators.h

    # Library TU — contains ProcessFreeFunctions + helpers
    MHeaderToolLib.cpp
)

# Library: shared by executable + tests
add_library(mht_lib STATIC
    MHeaderToolLib.cpp
)
target_link_libraries(mht_lib PUBLIC
    mession_core
)
target_include_directories(mht_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Source
)
configure_mession_compile_options(mht_lib)
target_compile_options(mht_lib PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/GR->
)

# Executable: thin wrapper holding main()
add_executable(MHeaderTool
    MHeaderTool.cpp
)
target_link_libraries(MHeaderTool PRIVATE
    mht_lib
)
configure_mession_compile_options(MHeaderTool)
target_compile_options(MHeaderTool PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/GR->
)

# Test executable (P4)
add_executable(FreeAsyncFuncGenTest
    Tests/FreeAsyncFuncGenTest.cpp
)
target_link_libraries(FreeAsyncFuncGenTest PRIVATE
    mht_lib
    mession_core
)
target_include_directories(FreeAsyncFuncGenTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Source
)
configure_mession_compile_options(FreeAsyncFuncGenTest)
```

- [ ] **Step 2: Extract `ProcessFreeFunctions` into `MHeaderToolLib.cpp`**

Create `/root/Mession/Source/Tools/MHeaderTool/MHeaderToolLib.cpp` with `ProcessFreeFunctions` (copy from Task 2 Step 2 verbatim) plus the `MHT` namespace alias. Required include preamble:

```cpp
#include "Core/Types.h"
#include "Parsing/FunctionParser.h"
#include "Util/StringUtil.h"

#include <cctype>
#include <filesystem>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace MHT = MHeaderTool;

// (paste the ProcessFreeFunctions body from Task 2 Step 2 here, minus the
//  `namespace MHT = MHeaderTool;` line above)
```

Then **remove** the inline definition of `ProcessFreeFunctions` from `MHeaderTool.cpp` and replace with:

```cpp
#include "MHeaderToolLib.cpp"  // pulls in ProcessFreeFunctions

namespace MHeaderTool
{
// declared in MHeaderToolLib.cpp
}
```

(`MHeaderTool.cpp` keeps its `int main`.)

- [ ] **Step 3: Create the test cpp from scratch**

```cpp
#include "Core/Types.h"
#include "Generation/CodeGenerator.h"
#include "MHeaderToolLib.cpp"  // ProcessFreeFunctions + MHT alias

#include "Common/Runtime/Log/Tests/TestHarness.h"

#include <cstdio>
#include <map>
#include <stdexcept>
#include <string>

namespace MHT = MHeaderTool;
namespace fs = std::filesystem;

static std::map<fs::path, std::string> OneHeader(
    const std::string& HeaderName,
    const std::string& Body)
{
    std::map<fs::path, std::string> M;
    M[fs::path("/tmp/") / HeaderName] = Body;
    return M;
}

TEST_CASE(FreeAsyncFunc_PlainAsync_Accepted)
{
    const std::string Header =
        "#pragma once\n"
        "namespace myns {\n"
        "MFUNCTION(Async)\n"
        "SFutureResult<int> ComputeAsync(int Seed);\n"
        "}\n";
    auto Contents = OneHeader("ComputeAsync.h", Header);
    auto Funcs = MHT::ProcessFreeFunctions(Contents);
    EXPECT_TRUE(Funcs.size() == 1);
    EXPECT_TRUE(Funcs[0].Name == "ComputeAsync");
    EXPECT_TRUE(Funcs[0].ResponseType == "int");

    // Round-trip: feed into the generator and verify the Frame struct name.
    MHT::CodeGenerator Gen(MHT::SOptions{});
    std::string Out = Gen.EmitFreeAsyncFramesHeader(Funcs, fs::path("/tmp/ComputeAsync.h"));
    EXPECT_TRUE(Out.find("MHeaderTool_AsyncFrame_Free_ComputeAsync") != std::string::npos);
    EXPECT_TRUE(Out.find("SFutureResult<int> AwaitedSlot;") != std::string::npos);
    EXPECT_TRUE(Out.find("int StoredValue") != std::string::npos);
}

TEST_CASE(FreeAsyncFunc_ServerCallAsync_Rejected)
{
    const std::string Header =
        "#pragma once\n"
        "namespace myns {\n"
        "MFUNCTION(ServerCall, Async)\n"
        "SFutureResult<int> BadAsync(int Seed);\n"
        "}\n";
    auto Contents = OneHeader("BadAsync.h", Header);
    bool bThrew = false;
    try
    {
        MHT::ProcessFreeFunctions(Contents);
    }
    catch (const std::runtime_error& ex)
    {
        bThrew = true;
        const std::string Msg = ex.what();
        EXPECT_TRUE(Msg.find("ServerCall") != std::string::npos);
        EXPECT_TRUE(Msg.find("2026-07-24") != std::string::npos);
        EXPECT_TRUE(Msg.find("2026-07-28") != std::string::npos);
    }
    EXPECT_TRUE(bThrew);
}

int main()
{
    std::printf("Running FreeAsyncFuncGenTest (P4)\n");
    std::printf("[ FreeAsyncFunc_PlainAsync_Accepted ]\n");
    Test_FreeAsyncFunc_PlainAsync_Accepted();
    std::printf("[ FreeAsyncFunc_ServerCallAsync_Rejected ]\n");
    Test_FreeAsyncFunc_ServerCallAsync_Rejected();
    RUN_TESTS();
    return 0;
}
```

- [ ] **Step 4: Verify the test compiles + passes**

Run: `cmake --build /root/Mession/Build --target FreeAsyncFuncGenTest -j4 && /root/Mession/Bin/FreeAsyncFuncGenTest`
Expected: both test cases pass; stdout shows `[  OK  ]` markers from `RUN_TESTS()`. If `ProcessFreeFunctions` has any bug, the test will fail with a clear assertion message — fix the implementation (Task 2 code) until the test is green.

- [ ] **Step 5: Commit**

```bash
git add Source/Tools/MHeaderTool/CMakeLists.txt \
        Source/Tools/MHeaderTool/MHeaderToolLib.cpp \
        Source/Tools/MHeaderTool/MHeaderTool.cpp \
        Source/Tools/MHeaderTool/Tests/FreeAsyncFuncGenTest.cpp
git commit -m "test(mht): FreeAsyncFuncGenTest covers plain Async + transport-tag reject"
```

---

### Task 5: Delete `MAwait` / `MAwaitOk` / `TPlayerCommandFuture` in FiberAwait.h

**Files:**
- Modify: `/root/Mession/Source/Common/Runtime/Concurrency/FiberAwait.h` (delete the lines listed below)
- Verify: `/root/Mession/Source/Common/Runtime/Concurrency/FiberAwait.cpp` (no change — see Step 3)

**Interfaces:**
- Consumes: nothing
- Produces: `FiberAwait.h` is shorter; the file still exports `MHasCurrentPlayerCommand`, `MCurrentPlayerCommand`, `MCheckPoint`, `MYield`, and `MPlayerCommandDetail::*` (SuspendCurrentCommandUntil, CheckPointOrAbort, YieldCurrentCommand, FPlayerCommandAbort, FPlayerCommandError, BuildAwaitErrorMessage). `MFiberScheduler.h` consumers are unaffected.

- [ ] **Step 1: Delete the following line ranges in FiberAwait.h**

Delete these line ranges exactly (as displayed by `cat -n FiberAwait.h`):

| Lines | Content | Reason |
|-------|---------|--------|
| 15-16 | `template<typename T>` / `using TPlayerCommandFuture = MFuture<TResult<T, FAppError>>;` + the blank line below | alias has no consumer post-deletion (confirmed by `grep -rn 'TPlayerCommandFuture' Source/` only hits this file) |
| 100-103 | `template<typename T>` / `T MAwait(MFuture<T> Future);` / blank / `void MAwait(MFuture<void> Future);` | forward decls of the MAwait family |
| 105-117 | `template<typename T>` / `T MAwaitOk(SFutureResult<T> Future);` / blank / comment block / `template<typename T>` / `[[deprecated(...)]]` / `T MAwaitOk(TPlayerCommandFuture<T> Future);` / blank / `void MAwaitOk(SFutureResult<void> Future);` / blank / `[[deprecated(...)]]` / `void MAwaitOk(TPlayerCommandFuture<void> Future);` | forward decls of the MAwaitOk family (both SF and TPlayerCommandFuture overloads) |
| 137-189 | `template<typename T>` / `T MAwait(MFuture<T> Future)` body (full template impl) | MAwait implementation |
| 191-235 | `inline void MAwait(MFuture<void> Future)` body | MAwait<void> implementation |
| 237-253 | `template<typename T>` / `T MAwaitOk(SFutureResult<T> Future)` body | MAwaitOk<T> implementation |
| 255-260 | `template<typename T>` / `[[deprecated]]` / `T MAwaitOk(TPlayerCommandFuture<T> Future)` body | deprecated MAwaitOk<TPlayerCommandFuture<T>> impl |
| 262-296 | `inline void MAwaitOk(SFutureResult<void> Future)` body | MAwaitOk<void> implementation |
| 298-301 | `inline void MAwaitOk(TPlayerCommandFuture<void> Future)` body | deprecated MAwaitOk<TPlayerCommandFuture<void>> impl |

After deletion, the file should contain only the preserved-API block (lines 1-14, 17-98, plus the `MPlayerCommandDetail::BuildAwaitErrorMessage` inline at 119-134).

- [ ] **Step 2: Update the legacy banner comment**

The block at lines 10-11 currently says `// NOTE: legacy ucontext-backed path; new code should use the spec §7 ...` Append the deletion note in the same block:

```cpp
// NOTE: legacy ucontext-backed path; new code should use the spec §7
// state-machine async/await model. See Docs/superpowers/specs/2026-07-24-cpp17-async-await.md.
// P4 (spec 2026-07-28 §C): MAwait / MAwaitOk / TPlayerCommandFuture deleted
// (2026-07-28). The remaining surface here is the player-command runtime
// plumbing (MHasCurrentPlayerCommand / MCurrentPlayerCommand / MCheckPoint /
// MYield / MPlayerCommandDetail::SuspendCurrentCommandUntil), which is
// kept in place for callers that legitimately run inside MFiberScheduler.
```

- [ ] **Step 3: Verify FiberAwait.cpp does not reference the deleted symbols**

Run: `grep -n 'MAwait\|MAwaitOk\|TPlayerCommandFuture' /root/Mession/Source/Common/Runtime/Concurrency/FiberAwait.cpp`
Expected: empty (no matches). `FiberAwait.cpp` only uses `MFiberScheduler` + `CommandExecutionContext` + the `MPlayerCommandDetail::*` functions — none of the deleted APIs are referenced.

- [ ] **Step 4: Build the project**

Run: `cmake --build /root/Mession/Build -j4 2>&1 | tee /tmp/p4_t5_build.log`
Expected: 0 errors. If a caller of the deleted APIs surfaces (spec §6 risk register says 0 callers), the compiler will flag the file — fix in this same commit (no scope expansion). The grep above should be the final word; if anything else compiles, treat it as a real bug.

- [ ] **Step 5: Commit**

```bash
git add Source/Common/Runtime/Concurrency/FiberAwait.h
git commit -m "refactor(async): delete MAwait/MAwaitOk/TPlayerCommandFuture (P4 §C)"
```

---

### Task 6: Rewrite the two legacy-API references in MAsync.h + MRpcChannel.h

**Files:**
- Modify: `/root/Mession/Source/Common/Runtime/Async/MAsync.h:236-238` (the "便捷别名" section header)
- Modify: `/root/Mession/Source/Common/Net/Rpc/MRpcChannel.h:13-30` (the file doxygen block)

**Interfaces:**
- Consumes: nothing
- Produces: source comments that no longer reference deleted APIs

- [ ] **Step 1: Rewrite the MAsync.h line 237 comment**

Replace the three-line block at lines 236-238:

```cpp
// ============================================
// 便捷别名（与现有 TPlayerCommandFuture 兼容）
// ============================================
```

with:

```cpp
// ============================================
// 便捷别名（与 MFuture<TResult<T, FAppError>> 形态一致）
// ============================================
//
// 业务统一走 SFutureResult<T>（spec §5.1）。TAsyncFuture 是历史遗留的
// 透传别名（P4 删除 MAwait / TPlayerCommandFuture 后已无 consumer）；
// 新代码禁止使用，编译器会以 deprecated 警告报错。
```

- [ ] **Step 2: Rewrite the MRpcChannel.h line 26 doxygen example**

Replace the doxygen block at lines 13-30 (the example usage of `MAwaitOk`) with:

```cpp
/**
 * MRpcChannel - 统一 RPC 调用通道
 *
 * 统一 ServerCall 和 ClientCall，提供一致的异步调用体验。
 *
 * Server-side transport resolution（选 connection / lazy connect）由
 * MEndpointCache 全局单例负责——Caller 不用再传 IRpcTransportResolver。
 *
 * 用法（spec 2026-07-24 §18 附录 A — P4 后不再使用 MAwaitOk）:
 *
 * // ServerCall: 调用远程服务器
 * auto fut = MRpcChannel::Get().Call<FResponse>(
 *     EServerType::Echo, "MEchoService", "Echo", request);
 * fut.Then([](SFutureResult<FResponse> F)
 * {
 *     if (F.IsOk()) { /* 用 F.GetResult().GetValue() */ }
 *     else          { /* 处理 F.GetError() */ }
 * });
 *
 * // ClientCall: 发送到客户端
 * MRpcChannel::Get().SendToClient(connection, "MPlayerController", "OnNotify", response);
 */
```

- [ ] **Step 3: Verify the touched comments are gone**

Run: `grep -n 'MAwaitOk\|TPlayerCommandFuture' /root/Mession/Source/Common/Runtime/Async/MAsync.h /root/Mession/Source/Common/Net/Rpc/MRpcChannel.h`
Expected: empty (both files clean).

- [ ] **Step 4: Commit**

```bash
git add Source/Common/Runtime/Async/MAsync.h Source/Common/Net/Rpc/MRpcChannel.h
git commit -m "docs(async): drop MAwaitOk / TPlayerCommandFuture from comments (P4 §C followup)"
```

---

### Task 7: Sync the parent async spec (deprecate → remove)

**Files:**
- Modify: `/root/Mession/Docs/superpowers/specs/2026-07-24-cpp17-async-await.md`

**Interfaces:**
- Consumes: nothing
- Produces: a parent spec where §2 / §6.2 / §7.4 / §14 / §16.8 / §17 Q4 no longer contradict the deleted `MAwait` API or the never-shipped `MASYNC` macro

- [ ] **Step 1: §2 — Non-goals (line 39)**

Replace the second non-goal at line 39:

```markdown
2. **全站有栈 fiber 作为默认执行模型**（现有 `MFiberScheduler` / `MAwait` 不扩展为主 API）。
```

with:

```markdown
2. **全站有栈 fiber 作为默认执行模型**（`MFiberScheduler` 保留作 player-command 基础设施；`MAwait` / `MAwaitOk` 已于 P4 删除）。
```

- [ ] **Step 2: §6.2 — Internal async functions (lines 123-135)**

Replace the entire §6.2 block (lines 123-135, ends at the `// v1 默认...` comment) with:

```markdown
### 6.2 内部异步函数（非 RPC 入口）

RPC 入口用 `MFUNCTION`；工具函数（自由函数、namespace-scope helper）需要 await 时，使用 **`MFUNCTION(Async)`** 标记——与类成员路径走完全相同的 MHeaderTool 状态机生成（spec §7.2；P4 引入，见 `2026-07-28-async-p4-wrap.md` §B）。

```cpp
// 自由函数示例（P4 之后）—— 与类成员 MFUNCTION(..., Async) 等价
MFUNCTION(Async)
SFutureResult<FFoo> LoadFooAsync(int Seed);
```

- **不**做「仅因返回 `SFutureResult` 就自动当 async」的静默推断（避免误生成）。
- 自由函数上 `MFUNCTION(Async)` 不允许带 `ServerCall` / `ClientCall` / `RPC` 等 transport tag——MHeaderTool 会在该误用上报错并引用 `2026-07-28` spec。
- 若实现阶段证明宏噪音过大，可开附录变体「返回类型 + 含 AWAIT 才生成」；默认仍是显式标记。
- P0–P3 期间曾考虑过的 `MASYNC` 宏已由 P4 决定**不引入**（`MFUNCTION(Async)` 同时覆盖类成员与自由函数）。
```

- [ ] **Step 3: §7.4 — Fiber `MAwait` isolation (lines 207-216)**

Replace the table + following paragraph (lines 207-216) with:

```markdown
### 7.4 与 Fiber 的隔离

| | `AWAIT`（本 spec） | Fiber path（`MFiberScheduler`） |
|--|-------------------|--------------------------------|
| 模型 | 无栈状态机语义 | 有栈 ucontext fiber |
| 入口 | Async 状态机 Frame | `MFiberScheduler::CreateExecution` |
| 主路径 | **是** | **否（仅 player-command 基础设施）** |
| Windows | 与 Linux 同一套状态机 | null backend 不可 suspend |

P4 收口后：`MAwait` / `MAwaitOk` / `TPlayerCommandFuture` 已删除（`2026-07-28-async-p4-wrap.md` §C）。`FiberAwait.h` 仅保留 player-command runtime 钩子（`MHasCurrentPlayerCommand` / `MCurrentPlayerCommand` / `MCheckPoint` / `MYield` / `MPlayerCommandDetail::SuspendCurrentCommandUntil`）。业务层 handler 一律走本 spec 状态机路径。
```

- [ ] **Step 4: §14 — Phased rollout (P4 row, line 401)**

Replace the P4 row in the table at line 401:

```markdown
| **P4 收口** | `MASYNC` 内部函数；Fiber API 文档退役；风格/CLAUDE 快查 | 新异步代码只走本 spec 路径 |
```

with:

```markdown
| **P4 收口** | `MFUNCTION(Async)` 扩展到自由函数（不再引入 `MASYNC`）；删除 `MAwait` / `MAwaitOk` / `TPlayerCommandFuture`；父 spec 同步升级；`CLAUDE.md` 加 async 快查表 | 新异步代码只走本 spec 路径；grep `MAwait` / `MASYNC` / `TPlayerCommandFuture` 仅命中已废弃的基础设施注释 |
```

- [ ] **Step 5: §16.8 — Decision record (line 427)**

Replace item 8 at line 427:

```markdown
8. **Fiber / `MAwait`** = legacy，非 C# 向主模型。
```

with:

```markdown
8. **Fiber** = 仅作 player-command 基础设施保留；`MAwait` / `MAwaitOk` / `TPlayerCommandFuture` 已于 P4 删除（`2026-07-28-async-p4-wrap.md` §C），不再是非主模型——已**移除**。
```

- [ ] **Step 6: §17 — Open questions (Q4 row, line 438)**

Delete the Q4 row at line 438:

```markdown
| Q4 | `MASYNC` 宏拼写/放哪头文件？ | `AwaitMacros.h` 或 `MAsync.h` |
```

(Replace the entire `| Q4 | ...` line and its trailing newline with empty content so the table has one fewer row.)

- [ ] **Step 7: Appendix C — Revision history**

Add a new row at the end of the table at line 484:

```markdown
| 2026-07-28 | v2：P4 收口后修订——`MASYNC` 不引入；`MAwait` / `MAwaitOk` / `TPlayerCommandFuture` 删除；§6.2 / §7.4 / §14 / §16.8 / §17 Q4 同步（见 `2026-07-28-async-p4-wrap.md`） |
```

- [ ] **Step 8: Verify no remaining MASYNC/MAwait references in the spec**

Run: `grep -n 'MASYNC\|MAwait\|TPlayerCommandFuture' /root/Mession/Docs/superpowers/specs/2026-07-24-cpp17-async-await.md`
Expected: no hits. (`MAwaitOk` is also gone.)

- [ ] **Step 9: Commit**

```bash
git add Docs/superpowers/specs/2026-07-24-cpp17-async-await.md
git commit -m "docs(async): parent spec P4 sync — MASYNC removed, MAwait removed (spec v2)"
```

---

### Task 8: Add async quick-reference to CLAUDE.md

**Files:**
- Modify: `/root/Mession/CLAUDE.md` (insert new section between the existing `## Reflection system` block and `## Recommended reading`)

**Interfaces:**
- Consumes: nothing
- Produces: a 5-10 line cheat sheet in CLAUDE.md that points engineers at the parent spec and the P4 wrap spec without duplicating their content

- [ ] **Step 1: Insert the section**

Insert immediately before the `## Recommended reading` line (line 127). The exact placement: after the last paragraph of `## Reflection system` (ends at "ClientCall stable IDs default from function identity; use `Api=...` / `ClientApi=...` when renames must keep wire IDs." on line 125) and before the `## Recommended reading` heading.

```markdown
## C++17 async model — quick reference

- **Contract**: every async function returns `SFutureResult<T>` (no exceptions on the result path; `Get()` throws `FFutureResultError` on err). The legacy `MFUTURE(T)` macro is gone.
- **Mark async methods**: `MFUNCTION(..., Async)` (class members) or `MFUNCTION(Async)` (free functions). `MASYNC` was considered in P0–P3 and dropped in P4 — do not reintroduce.
- **Await**: `AWAIT_OK(expr)` — only inside an `Async` function; the macro expands to `Frame->AwaitOk(expr)` so a local `Frame` must exist in scope.
- **Sync barrier**: `F.Get()` / `F.Wait()` outside Async functions. Never on the event-loop thread for a future that depends on that loop (spec §8.2 redline — `MAsyncContext::IsSameContext` triggers assert in DEBUG).
- **Fiber is legacy**: `MAwait` / `MAwaitOk` / `TPlayerCommandFuture` deleted in P4. `MFiberScheduler` remains only for player-command infrastructure.
- Full design: `Docs/superpowers/specs/2026-07-24-cpp17-async-await.md`; P4 wrap (free-func codegen, deletions): `Docs/superpowers/specs/2026-07-28-async-p4-wrap.md`.

```

- [ ] **Step 2: Verify the section is present and well-placed**

Run: `grep -n 'C++17 async model — quick reference\|Recommended reading' /root/Mession/CLAUDE.md`
Expected: the new section header appears immediately before `## Recommended reading`.

- [ ] **Step 3: Commit**

```bash
git add CLAUDE.md
git commit -m "docs(async): CLAUDE.md quick-reference for C++17 async model"
```

---

### Task 9: Final verification — full build, regression tests, residual grep

**Files:**
- Read-only verification; no file changes

**Interfaces:**
- Consumes: all prior tasks' outputs
- Produces: a green build + green regression tests + a clean grep that confirms only the spec history mentions the deleted symbols

- [ ] **Step 1: Full build**

Run: `cmake --build /root/Mession/Build -j4 2>&1 | tee /tmp/p4_full_build.log`
Expected: 0 errors. (The `MFiberScheduler.h` itself may still print a `[[deprecated]]` warning — that is pre-existing and out of scope; spec §1.2 explicitly excludes the infrastructure.)

- [ ] **Step 2: LogTest regression**

Run: `cmake --build /root/Mession/Build --target LogTest -j4 && /root/Mession/Bin/LogTest`
Expected: 84/84 cases pass (same as pre-P4 baseline; this run is a regression sanity check).

- [ ] **Step 3: Protocol reflection regression**

Run: `python3 /root/Mession/Scripts/verify_protocol.py`
Expected: same output as pre-P4 — P4 does not change the reflection contract (no `MPROPERTY` rename/reorder).

- [ ] **Step 4: P3 AsyncFrameTest regression**

Run: `cmake --build /root/Mession/Build --target AsyncFrameTest -j4 && /root/Mession/Bin/AsyncFrameTest`
Expected: 3/3 cases pass (P3 baseline preserved).

- [ ] **Step 5: P4 FreeAsyncFuncGenTest**

Run: `cmake --build /root/Mession/Build --target FreeAsyncFuncGenTest -j4 && /root/Mession/Bin/FreeAsyncFuncGenTest`
Expected: 2/2 cases pass (the test added in Task 4).

- [ ] **Step 6: Residual grep — deleted APIs**

Run:
```bash
grep -rn 'MAwait\b\|MAwaitOk\b\|TPlayerCommandFuture\b' /root/Mession/Source /root/Mession/Build/Generated /root/Mession/Docs 2>/dev/null
```
Expected: only matches are inside the parent spec revision history (Task 7) and the P4 wrap spec (which is the source of truth). No matches in `Source/`, no matches in `Build/Generated/`.

- [ ] **Step 7: Residual grep — MASYNC macro**

Run:
```bash
grep -rn 'MASYNC\b' /root/Mession/Source /root/Mession/Build/Generated /root/Mession/Docs 2>/dev/null
```
Expected: only matches are inside the P0 cleanup spec (`2026-07-25-cpp17-p0-cleanup.md`) and the P4 wrap spec (revision history + D1 decision table). No matches in `Source/`, no matches in `Build/Generated/`.

- [ ] **Step 8: Commit (only if any fix-up was needed)**

If Steps 1-7 surfaced a residual that had to be patched, commit the patch in this final task — no further commits after this. Subject style: `fix(async): <one-line description of what was patched>`. If everything was clean, this step is a no-op (no commit).

- [ ] **Step 9: PR-ready summary**

Write the PR description in the standard `feat:` / `refactor:` / `docs:` per-area structure. The summary should call out:

- MHeaderTool grows free-function codegen (`ProcessFreeFunctions` + `EmitFreeAsyncFramesHeader`).
- Fiber legacy API (`MAwait` / `MAwaitOk` / `TPlayerCommandFuture`) deleted.
- Two legacy doxygen references in `MAsync.h` and `MRpcChannel.h` rewritten.
- Parent spec §2 / §6.2 / §7.4 / §14 / §16.8 / §17 Q4 synced (v2).
- `CLAUDE.md` gets an async quick-reference.
- New `FreeAsyncFuncGenTest` target; P3 `AsyncFrameTest` + `LogTest` + `verify_protocol.py` regressions green.
- File count: 2 created + 9 modified = 11 (within ≤12 limit).

No commit for this step — it is the PR body, written in the GitHub UI / `gh pr create` body.

---

### Critical Files for Implementation

- `/root/Mession/Source/Tools/MHeaderTool/Generation/CodeGenerator.h`
- `/root/Mession/Source/Tools/MHeaderTool/MHeaderTool.cpp`
- `/root/Mession/Source/Common/Runtime/Concurrency/FiberAwait.h`
- `/root/Mession/Docs/superpowers/specs/2026-07-24-cpp17-async-await.md`
- `/root/Mession/Source/Tools/MHeaderTool/Tests/FreeAsyncFuncGenTest.cpp`
