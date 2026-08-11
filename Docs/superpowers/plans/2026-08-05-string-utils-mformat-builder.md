# MFormat + MStringBuilder Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 `Source/Common/Runtime/StringUtils.h` 增加 `MFormat`(fmt 包装)与 `MStringBuilder`(`TByteArray` 后端 + 自由函数)两个标准库工具,并把 `TStringView` 修正为 `MStringView`。

**Architecture:**
- `MFormat::Format/ToString` 是 `fmt::vformat` 的薄包装,统一替代 `std::to_string`/`snprintf`。
- `MStringBuilder` 是数据类(只暴露状态查询 + Buffer 取址给 `fmt::format_to`),所有变更操作是 `namespace MStringBuilder` 下的自由函数,builder 作为第一引用参数。
- 编译期 fmt 接入:`FetchContent` 拉 fmt 11.0.2,header-only,`mession_core` PUBLIC 链接。

**Tech Stack:** C++17, fmt 11.0.2 (FetchContent), CMake, 项目自研 TestHarness(仿 `Source/Common/Runtime/Log/Tests/TestHarness.h`)。

## Global Constraints

- **C++ 标准:** `CMAKE_CXX_STANDARD = 17`
- **命名:** `S*` 结构 / `M*` 类 / `E*` 枚举 / `I*` 接口 / `T*` 模板别名 / `M*` 非模板别名 / `bXxx` 布尔 / PascalCase 函数
- **Builder API 规约:** 任何"对 builder 做一件事"的函数都是 `namespace MStringBuilder` 自由函数,builder 作为第一引用参数;**不返回 `MStringBuilder&`**,不 method chaining。
- **`TStringView` 错误命名修正:** 全项目 grep 重命名为 `MStringView`(非模板,`M*` 前缀)。命名约束已在 2026-08-05 设计文档定稿。
- **依赖:** `fmt` 通过 `FetchContent` 拉取 11.0.2,`mession_core` PUBLIC 链接 `fmt::fmt-header-only`。**不留新三方依赖**。
- **构建系统:** CMake;`mession_core` 沿用现有文件列表(非 `file(GLOB)`);`StringUtilsTest` 仿 `LogTest` 结构(main.cpp 内 `#include` 测试 TU)。
- **代码风格:** `Docs/CodingStyle.md` — 4 空格缩进、Allman 大括号、列宽 240、指针星号左对齐 `T* Ptr`。
- **测试框架:** 项目已有 `TestHarness.h`(EXPECT_TRUE/EXPECT_EQ/EXPECT_NE/TEST_CASE/RUN_TESTS 宏)。`StringUtilsTest` 复用同一框架,**不引入新测试框架**。
- **Commit 信息:** 描述实际改动,不写 P* 等任务阶段代号,不写 AI 归属行。

---

## 文件映射

### 新增文件

| 文件 | 用途 |
|---|---|
| `Source/Common/Runtime/Tests/StringUtilsTestMain.cpp` | StringUtilsTest 入口,仿 `LogTest/main.cpp` 把测试 TU 通过 `#include` 拉入 |
| `Source/Common/Runtime/Tests/TestStringUtilsFormat.cpp` | `Test_MFormat_Format_*` / `Test_MFormat_ToString_*` 测试用例 |
| `Source/Common/Runtime/Tests/TestStringUtilsBuilder.cpp` | `Test_MStringBuilder_*` 测试用例 |

### 修改文件

| 文件 | 改动 |
|---|---|
| `CMakeLists.txt` | 插入 `include(FetchContent)` + `FetchContent_Declare(fmt ...)` + `target_link_libraries(mession_core PUBLIC fmt::fmt-header-only)` + 新增 `StringUtilsTest` 可执行目标 |
| `Source/Common/Runtime/MLib.h` | `using TStringView = std::string_view;` → `using MStringView = std::string_view;` |
| `Source/Common/Runtime/StringUtils.h` | 全文件 `TStringView` → `MStringView`;在文件内追加 `namespace MFormat` 与 `class MStringBuilder` + `namespace MStringBuilder` 自由函数;6 个 `MStringUtil::ToString` 重载重写为单模板,内部委托 `MFormat::ToString<T>` |
| `Source/Common/Runtime/Log/LogContext.h` | `TStringView` → `MStringView` |
| `Source/Common/Runtime/Log/LogContext.cpp` | `TStringView` → `MStringView` |
| `Source/Common/Runtime/Log/ConsoleSink.cpp` | line 41: `std::snprintf(FmtBuf, 64, "%04d-...%03dZ", ...)` → `auto FmtBuf = MFormat::Format("{:%Y}-...", ...)`(ISO-8601 改用 `fmt::format` 的 `{:%FT%T}.{:S}Z` 形式) |
| `Source/Common/Runtime/Log/RollingFileSink.cpp` | line 51(ISO-8601 时间戳)、line 146/157/188(`"%s.%d"` 文件名)、line 263(header JSON) 共 5 处 `snprintf` → `MFormat::Format` |
| `Source/Common/Runtime/Log/CoredumpSink.cpp` | line 111(`snprintf(Esc, ..., "\\u%04x", C)`)、line 188(`snprintf(Pid, ..., "%d", ::getpid())`) → `MFormat::Format` |
| `Source/Common/Runtime/Log/Log.cpp` | line 64:`FormatInline` 函数 `std::vsnprintf` → `fmt::vformat_to`(`FormatInline` 内部用 `fmt::vformat_to` 写到固定 buffer,签名 `size_t FormatInline(MStringView Fmt, va_list Args, char* Out, size_t OutSize)`;在调用点把 `va_list` 通过 `fmt::make_format_args` 桥接) |
| `Source/Common/Runtime/Json.h` | line 308:`std::snprintf(Buf, sizeof(Buf), "\\u%04x", C & 0xff)` → `MFormat::Format("\\u{:04x}", C & 0xff)`,用 `Out += result` 追加 |

---

## 任务清单

### Task 1: 全项目重命名 `TStringView` → `MStringView`

**Files:**
- Modify: `Source/Common/Runtime/MLib.h:164`
- Modify: `Source/Common/Runtime/StringUtils.h`(全文,~10 处)
- Modify: `Source/Common/Runtime/Log/LogContext.h`(`Set`/`Unset` 签名 + RAII 字段类型)
- Modify: `Source/Common/Runtime/Log/LogContext.cpp`(`Set`/`Unset` 函数体)

**为什么先做:** 设计文档规定 `MStringView` 是 `MFormat`/`MStringBuilder` API 的入参类型。先把命名统一,后续任务可以无障碍引用 `MStringView`。

- [ ] **Step 1: 在 `MLib.h` 中重命名别名**

打开 `/root/Mession/Source/Common/Runtime/MLib.h`,定位:
```cpp
#if MESSION_CPLUSPLUS >= 201703L
using TStringView = std::string_view;
#endif
```
改为:
```cpp
#if MESSION_CPLUSPLUS >= 201703L
using MStringView = std::string_view;
#endif
```

- [ ] **Step 2: 在 `StringUtils.h` 中重命名所有 `TStringView`**

打开 `/root/Mession/Source/Common/Runtime/StringUtils.h`,将文件中所有 `TStringView`(共 ~10 处:line 111/115/117/120/123/128/134/140/142 与注释 line 108)替换为 `MStringView`。**注释 line 108 一并改为 `// MStringView 工具:`**。

- [ ] **Step 3: 重命名 `Log/LogContext.h` 与 `Log/LogContext.cpp`**

打开这两个文件,把 `TStringView` 全部替换为 `MStringView`。这是项目里仅有的两个 log 调用方(grep 验证),不涉及实现改动。

- [ ] **Step 4: 全项目 grep 验证无残留**

```bash
grep -rn "TStringView" /root/Mession/Source /root/Mession/CMakeLists.txt
```
预期输出为空(无残留)。若仍有,补齐。

- [ ] **Step 5: 完整构建验证**

```bash
cmake --build Build -j4 2>&1 | tail -30
```
预期:构建成功(`Build succeeded` / 无 error 行)。

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "refactor(string-utils): rename TStringView to MStringView

TStringView 是 std::string_view 的别名而非模板,违反项目命名规约
(M* 用于非模板别名,T* 用于模板别名)。同步重命名 MLib.h、
StringUtils.h、Log/LogContext.h/.cpp 内全部引用。"
```

---

### Task 2: CMake 接入 fmt(FetchContent + header-only 链接)

**Files:**
- Modify: `/root/Mession/CMakeLists.txt`

**为什么独立成任务:** fmt 是新三方依赖,先用纯 CMake 改动验证 `FetchContent` 配置成功,把后续任务的"构建失败"风险隔离在此。

- [ ] **Step 1: 在 `CMakeLists.txt` 顶部加 `include(FetchContent)`**

打开 `/root/Mession/CMakeLists.txt`,定位到 `project(...)` 行后(约 line 2-3),在 `option(MESSION_USE_MONGOCXX ...)` 之前插入:

```cmake
include(FetchContent)
```

- [ ] **Step 2: 在 `project(...)` 之后、`mession_core` 库定义之前,声明 fmt**

定位到 line 96 之前(`# Core 库` 注释上方),插入:

```cmake
# --- fmt (FetchContent, header-only) ---------------------------------------
set(FMT_HEADER_ONLY ON CACHE BOOL "Use fmt header-only build" FORCE)
set(FMT_DIR "" CACHE PATH "Local fmt source directory (skips FetchContent)")
if(FMT_DIR AND EXISTS "${FMT_DIR}/CMakeLists.txt")
    set(FMT_SOURCE_DIR "${FMT_DIR}")
else()
    FetchContent_Declare(
        fmt
        GIT_REPOSITORY https://github.com/fmtlib/fmt.git
        GIT_TAG        11.0.2
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(fmt)
endif()
```

- [ ] **Step 3: 把 `mession_core` PUBLIC 链接到 fmt::fmt-header-only**

定位到 line 115-120(原 `target_link_libraries(mession_core ...)`),改为:

```cmake
if(NOT WIN32)
    target_link_libraries(mession_core PUBLIC m)
endif()
if(WIN32)
    target_link_libraries(mession_core PUBLIC ws2_32)
endif()
target_link_libraries(mession_core PUBLIC fmt::fmt-header-only)
```

- [ ] **Step 4: 配置 + 构建验证(本次不写 fmt 调用代码,仅验证拉取/链接通过)**

```bash
cmake -S . -B Build 2>&1 | tail -40
cmake --build Build -j4 --target mession_core 2>&1 | tail -20
```
预期:`mession_core` 目标能构建(fmt 头文件已被间接包含);无 `fmt not found` / `undefined reference to fmt::*` 错误。

> **注意:** `mession_core` 当前未直接 `#include <fmt/...>`;Step 4 主要验证 FetchContent 配置成功。如果 `cmake -S` 配置期就失败,优先排查网络/版本/缓存。

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt
git commit -m "build: wire fmt 11.0.2 via FetchContent (header-only)

FetchContent 拉取 fmtlib/fmt 11.0.2,FMT_HEADER_ONLY=ON。mession_core
PUBLIC 链接 fmt::fmt-header-only,所有下游自动获得 <fmt/format.h>。
留 FMT_DIR 缓存变量供本地 vendor 副本指向以避免 FetchContent 网络拉取。"
```

---

### Task 3: TDD — `MFormat::Format` 与 `MFormat::ToString`

**Files:**
- Modify: `Source/Common/Runtime/StringUtils.h`(在文件顶部、`MStringUtil` 命名空间之前追加 `MFormat`)
- Create: `Source/Common/Runtime/Tests/TestStringUtilsFormat.cpp`(空骨架,Step 1 写第一个失败用例)
- Modify: `CMakeLists.txt`(新增 `StringUtilsTest` 目标)

**Interfaces:**
- Produces: `MFormat::Format<MStringView, TArgs...>(MStringView Fmt, TArgs&&...) -> MString`、`MFormat::ToString<T>(T) -> MString`

- [ ] **Step 1: 写失败用例 `Test_MFormat_Format_IntegerSubstitution`**

创建 `/root/Mession/Source/Common/Runtime/Tests/TestStringUtilsFormat.cpp`:

```cpp
#include "Common/Runtime/StringUtils.h"
#include "Common/Runtime/Tests/TestHarness.h"

TEST_CASE(MFormat_Format_IntegerSubstitution)
{
    EXPECT_EQ(MFormat::Format("id={}", 42), MString("id=42"));
    EXPECT_EQ(MFormat::Format("{} {} {}", 1, 2, 3), MString("1 2 3"));
}
```

> **TestHarness.h 复用** — 后续任务会把它复制到 `Tests/` 目录;此任务先让编译链接起来,Task 4 才统一生成。

- [ ] **Step 2: 注册 StringUtilsTest 目标,但暂不挂测试文件**

打开 `CMakeLists.txt`,定位到 `LogTest` 目标定义(line 513-525 附近),在它**之后**插入:

```cmake
# StringUtilsTest - MFormat / MStringBuilder unit tests
add_executable(StringUtilsTest
    Source/Common/Runtime/Tests/StringUtilsTestMain.cpp
    Source/Common/Runtime/Tests/TestStringUtilsFormat.cpp
)
target_include_directories(StringUtilsTest
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
)
target_link_libraries(StringUtilsTest
    PRIVATE
        mession_common
)
configure_mession_compile_options(StringUtilsTest)
```

**创建** `/root/Mession/Source/Common/Runtime/Tests/StringUtilsTestMain.cpp`(占位):

```cpp
#include <cstdio>

int main()
{
    std::printf("Running StringUtilsTest...\n");
    std::printf("=== placeholder, see Task 4 ===\n");
    return 0;
}
```

(后续 Task 4 会把测试入口升级成 `RUN_TESTS()`。)

- [ ] **Step 3: 构建 + 跑测试,预期编译失败(`MFormat` 未定义)**

```bash
cmake --build Build --target StringUtilsTest -j4 2>&1 | tail -20
```
预期:`error: 'MFormat' is not a namespace-name` / `'MFormat::Format' was not declared`。

- [ ] **Step 4: 在 `StringUtils.h` 中实现 `MFormat` 命名空间**

打开 `Source/Common/Runtime/StringUtils.h`,在 `#include` 块(line 1-17)后、`namespace MStringUtil` 前(line 20 前),插入:

```cpp
// --- fmt 薄包装 -------------------------------------------------------------
#include <fmt/format.h>

namespace MFormat
{
    // 主入口 — 替代 std::to_string / snprintf / ostringstream 拼装
    template <typename... TArgs>
    MString Format(MStringView Fmt, TArgs&&... Args)
    {
        return fmt::vformat(Fmt, fmt::make_format_args(std::forward<TArgs>(Args)...));
    }

    // 类型安全数值转字符串 — 替代 MStringUtil::ToString 重载
    template <typename T>
    MString ToString(T Value)
    {
        if constexpr (TIsSame<T, MString>)
        {
            return Value;
        }
        else if constexpr (std::is_arithmetic_v<T>)
        {
            return fmt::format("{}", Value);
        }
        else
        {
            return fmt::format("{}", Value);
        }
    }
}
```

`TIsSame` 已在 `MLib.h` 中定义;`std::is_arithmetic_v` 走 `<type_traits>`(由 `MLib.h` 间接包含)。

- [ ] **Step 5: 构建 + 跑测试,预期通过**

```bash
cmake --build Build --target StringUtilsTest -j4 2>&1 | tail -10
./Bin/StringUtilsTest
```
预期:测试用例 `MFormat_Format_IntegerSubstitution` 报告通过;退出码 0。

- [ ] **Step 6: 扩展测试覆盖更多形态**

在 `TestStringUtilsFormat.cpp` 追加:

```cpp
TEST_CASE(MFormat_Format_HexAndPadding)
{
    EXPECT_EQ(MFormat::Format("{:x}", 0xab), MString("ab"));
    EXPECT_EQ(MFormat::Format("{:08x}", 0xab), MString("000000ab"));
    EXPECT_EQ(MFormat::Format("{:.3f}", 3.14159), MString("3.142"));
}

TEST_CASE(MFormat_Format_InvalidRaises)
{
    bool bThrew = false;
    try { MFormat::Format("{5}", 1); } // 越界位置引用 → fmt 抛异常
    catch (const fmt::format_error&) { bThrew = true; }
    EXPECT_TRUE(bThrew);
}

TEST_CASE(MFormat_ToString_ArithmeticAndString)
{
    EXPECT_EQ(MFormat::ToString(int32(42)), MString("42"));
    EXPECT_EQ(MFormat::ToString(int64(-7)), MString("-7"));
    EXPECT_EQ(MFormat::ToString(uint32(0xff)), MString("255"));
    EXPECT_EQ(MFormat::ToString(double64(1.5)), MString("1.5"));
    EXPECT_EQ(MFormat::ToString(MString("plain")), MString("plain"));
}
```

- [ ] **Step 7: 跑全部测试,预期通过**

```bash
cmake --build Build --target StringUtilsTest -j4 && ./Bin/StringUtilsTest
```
预期:所有 `MFormat_*` 用例通过;退出码 0。

- [ ] **Step 8: Commit**

```bash
git add -A
git commit -m "feat(string-utils): add MFormat (fmt wrapper) + StringUtilsTest target

MFormat::Format/MFormat::ToString 包装 fmt::vformat/fmt::format,
统一替代 std::to_string / snprintf 入口。StringUtilsTest 目标仿
LogTest 结构(独立可执行 + TestHarness 宏)。"
```

---

### Task 4: TDD — `MStringBuilder` 数据类 + `Reserve/Clear/ToString` 自由函数

**Files:**
- Modify: `Source/Common/Runtime/StringUtils.h`(追加 `class MStringBuilder` + 自由函数)
- Create: `Source/Common/Runtime/Tests/TestHarness.h`(从 `Log/Tests/` 复制并迁移)
- Modify: `Source/Common/Runtime/Tests/TestStringUtilsFormat.cpp`(追加 Builder 用例,本任务内)
- Modify: `Source/Common/Runtime/Tests/StringUtilsTestMain.cpp`(升级入口调用 `RUN_TESTS()`)

**Interfaces:**
- Produces:
  - `class MStringBuilder { MStringBuilder(); explicit MStringBuilder(size_t); size_t Size() const; bool Empty() const; size_t Capacity() const; MStringView View() const; TByteArray& Buffer(); const TByteArray& Buffer() const; }`
  - `MStringBuilder::Reserve(MStringBuilder&, size_t)`
  - `MStringBuilder::Clear(MStringBuilder&)`
  - `MStringBuilder::ToString(const MStringBuilder&) -> MString`

- [ ] **Step 1: 迁移 `TestHarness.h` 到 `Tests/`**

复制 `/root/Mession/Source/Common/Runtime/Log/Tests/TestHarness.h` 到 `/root/Mession/Source/Common/Runtime/Tests/TestHarness.h`,内容保持完全一致(同一份文件直接 cp 即可)。

- [ ] **Step 2: 写失败用例 `Test_MStringBuilder_StateQueries`**

在 `TestStringUtilsFormat.cpp` 末尾追加(同文件复用,后续 Task 5 会拆出独立 TU):

```cpp
TEST_CASE(MStringBuilder_StateQueries)
{
    MStringBuilder B;
    EXPECT_TRUE(B.Empty());
    EXPECT_EQ(B.Size(), size_t(0));
    EXPECT_EQ(B.Capacity(), size_t(0));

    MStringBuilder B2(64);
    EXPECT_EQ(B2.Size(), size_t(0));
    EXPECT_TRUE(B2.Capacity() >= 64);

    MStringBuilder::Reserve(B, 128);
    EXPECT_TRUE(B.Capacity() >= 128);

    MStringBuilder::Clear(B);
    EXPECT_EQ(B.Size(), size_t(0));
}
```

- [ ] **Step 3: 跑测试,预期编译失败(`MStringBuilder` 未定义)**

```bash
cmake --build Build --target StringUtilsTest -j4 2>&1 | tail -10
```
预期:`error: 'MStringBuilder' was not declared`。

- [ ] **Step 4: 在 `StringUtils.h` 中追加 `MStringBuilder` 类 + 自由函数**

打开 `Source/Common/Runtime/StringUtils.h`,在 `MFormat` 命名空间之后(line 38 `}` 之后,`namespace MStringUtil` 之前)插入:

```cpp
// --- MStringBuilder: TByteArray 后端的流式构造器 --------------------------------
class MStringBuilder
{
public:
    MStringBuilder() = default;
    explicit MStringBuilder(size_t InitialCapacity)
    {
        Buffer.reserve(InitialCapacity);
    }

    size_t      Size()     const noexcept { return Buffer.size(); }
    bool        Empty()    const noexcept { return Buffer.empty(); }
    size_t      Capacity() const noexcept { return Buffer.capacity(); }
    MStringView View()     const noexcept
    {
        return MStringView(reinterpret_cast<const char*>(Buffer.data()), Buffer.size());
    }

    // 仅供 fmt::format_to(std::back_inserter(...)) 与外部算法访问
    TByteArray&       Buffer()       noexcept { return Buffer; }
    const TByteArray& Buffer() const noexcept { return Buffer; }

private:
    TByteArray Buffer;
};

namespace MStringBuilder
{
    inline void Reserve(MStringBuilder& Builder, size_t Capacity)
    {
        Builder.Buffer().reserve(Capacity);
    }

    inline void Clear(MStringBuilder& Builder) noexcept
    {
        Builder.Buffer().clear();
    }

    inline MString ToString(const MStringBuilder& Builder)
    {
        return MString(reinterpret_cast<const char*>(Builder.Buffer().data()), Builder.Buffer().size());
    }
}
```

- [ ] **Step 5: 跑测试,预期通过**

```bash
cmake --build Build --target StringUtilsTest -j4 && ./Bin/StringUtilsTest
```
预期:`MStringBuilder_StateQueries` 通过;退出码 0。

- [ ] **Step 6: 升级 `StringUtilsTestMain.cpp` 调用 `RUN_TESTS()`**

打开 `/root/Mession/Source/Common/Runtime/Tests/StringUtilsTestMain.cpp`,改为:

```cpp
#include "Common/Runtime/Tests/TestHarness.h"
#include "Common/Runtime/Tests/TestStringUtilsFormat.cpp"

int main()
{
    std::printf("Running StringUtilsTest...\n");

    std::printf("[ MFormat_Format_IntegerSubstitution ]\n");
    Test_MFormat_Format_IntegerSubstitution();

    std::printf("[ MFormat_Format_HexAndPadding ]\n");
    Test_MFormat_Format_HexAndPadding();

    std::printf("[ MFormat_Format_InvalidRaises ]\n");
    Test_MFormat_Format_InvalidRaises();

    std::printf("[ MFormat_ToString_ArithmeticAndString ]\n");
    Test_MFormat_ToString_ArithmeticAndString();

    std::printf("[ MStringBuilder_StateQueries ]\n");
    Test_MStringBuilder_StateQueries();

    RUN_TESTS();
}
```

- [ ] **Step 7: 跑测试,预期通过**

```bash
cmake --build Build --target StringUtilsTest -j4 && ./Bin/StringUtilsTest
```
预期:5 个用例全通过;退出码 0。

- [ ] **Step 8: Commit**

```bash
git add -A
git commit -m "feat(string-utils): add MStringBuilder data class + Reserve/Clear/ToString

MStringBuilder 内部 TByteArray,只暴露状态查询(Szie/Empty/Capacity/
View/Buffer);namespace MStringBuilder 下 Reserve/Clear/ToString 自由
函数以 Builder 为第一引用参数,无 method chaining。TestHarness.h
从 Log/Tests 迁移到 Runtime/Tests/ 共用。"
```

---

### Task 5: TDD — `MStringBuilder::Append*` + `AppendByte` + `AppendFormat` 自由函数

**Files:**
- Modify: `Source/Common/Runtime/StringUtils.h`(在 `namespace MStringBuilder` 内追加)
- Create: `Source/Common/Runtime/Tests/TestStringUtilsBuilder.cpp`(独立 TU,与 format 用例拆开)
- Modify: `Source/Common/Runtime/Tests/StringUtilsTestMain.cpp`(追加 `#include` + `printf` + 调用)
- Modify: `CMakeLists.txt`(追加 `TestStringUtilsBuilder.cpp` 到 StringUtilsTest 源列表)

**Interfaces:**
- Produces:
  - `MStringBuilder::Append(MStringBuilder&, MStringView)`
  - `MStringBuilder::Append(MStringBuilder&, char)`
  - `MStringBuilder::Append(MStringBuilder&, const char*)`
  - `MStringBuilder::Append(MStringBuilder&, const MString&)`
  - `MStringBuilder::AppendByte(MStringBuilder&, uint8)`
  - `MStringBuilder::AppendFormat(MStringBuilder&, MStringView, TArgs&&...)`

- [ ] **Step 1: 创建失败用例文件 `TestStringUtilsBuilder.cpp`**

```cpp
#include "Common/Runtime/StringUtils.h"
#include "Common/Runtime/Tests/TestHarness.h"

TEST_CASE(MStringBuilder_Append_AllOverloads)
{
    MStringBuilder B;

    MStringBuilder::Append(B, MStringView("view"));
    MStringBuilder::Append(B, '+');
    MStringBuilder::Append(B, "cstr");
    MStringBuilder::Append(B, MString("mstr"));
    MStringBuilder::AppendByte(B, uint8(0x0A));

    EXPECT_EQ(B.Size(), size_t(13));
    EXPECT_EQ(MStringBuilder::ToString(B), MString("view+cstrmstr\n"));
}

TEST_CASE(MStringBuilder_AppendFormat_MatchesFormat)
{
    MStringBuilder B;
    MStringBuilder::AppendFormat(B, "x={} y={:.1f}", 7, 3.14);
    EXPECT_EQ(MStringBuilder::ToString(B), MFormat::Format("x={} y={:.1f}", 7, 3.14));
}

TEST_CASE(MStringBuilder_AppendFormat_Streaming)
{
    MStringBuilder B(8); // 预 reserve
    MStringBuilder::Append(B, "id=");
    MStringBuilder::AppendFormat(B, "{}", 42);
    MStringBuilder::AppendByte(B, uint8(0x00));
    EXPECT_EQ(B.Size(), size_t(6));
    EXPECT_EQ(MStringBuilder::View(B), MStringView("id=42", 5));
}
```

> 注意 `View(B)` 是类成员,所以这里直接 `B.View()` 即可,但为了一致也可写成 `MStringBuilder::View(B)`——本任务用 `B.View()`(Task 6 之后会重构)。

实际调用:

```cpp
TEST_CASE(MStringBuilder_AppendFormat_Streaming)
{
    MStringBuilder B(8);
    MStringBuilder::Append(B, "id=");
    MStringBuilder::AppendFormat(B, "{}", 42);
    MStringBuilder::AppendByte(B, uint8(0x00));
    EXPECT_EQ(B.Size(), size_t(6));
    EXPECT_EQ(B.View(), MStringView("id=42", 5));
}
```

- [ ] **Step 2: 挂 `TestStringUtilsBuilder.cpp` 到 StringUtilsTest 目标**

打开 `CMakeLists.txt`,定位 `StringUtilsTest` 目标的 `add_executable(...)` 行,把:

```cmake
add_executable(StringUtilsTest
    Source/Common/Runtime/Tests/StringUtilsTestMain.cpp
    Source/Common/Runtime/Tests/TestStringUtilsFormat.cpp
)
```

改为:

```cmake
add_executable(StringUtilsTest
    Source/Common/Runtime/Tests/StringUtilsTestMain.cpp
    Source/Common/Runtime/Tests/TestStringUtilsFormat.cpp
    Source/Common/Runtime/Tests/TestStringUtilsBuilder.cpp
)
```

- [ ] **Step 3: 升级 `StringUtilsTestMain.cpp` 调用新用例**

打开 `StringUtilsTestMain.cpp`,改为:

```cpp
#include "Common/Runtime/Tests/TestHarness.h"
#include "Common/Runtime/Tests/TestStringUtilsFormat.cpp"
#include "Common/Runtime/Tests/TestStringUtilsBuilder.cpp"

int main()
{
    std::printf("Running StringUtilsTest...\n");

    std::printf("[ MFormat_Format_IntegerSubstitution ]\n");
    Test_MFormat_Format_IntegerSubstitution();

    std::printf("[ MFormat_Format_HexAndPadding ]\n");
    Test_MFormat_Format_HexAndPadding();

    std::printf("[ MFormat_Format_InvalidRaises ]\n");
    Test_MFormat_Format_InvalidRaises();

    std::printf("[ MFormat_ToString_ArithmeticAndString ]\n");
    Test_MFormat_ToString_ArithmeticAndString();

    std::printf("[ MStringBuilder_StateQueries ]\n");
    Test_MStringBuilder_StateQueries();

    std::printf("[ MStringBuilder_Append_AllOverloads ]\n");
    Test_MStringBuilder_Append_AllOverloads();

    std::printf("[ MStringBuilder_AppendFormat_MatchesFormat ]\n");
    Test_MStringBuilder_AppendFormat_MatchesFormat();

    std::printf("[ MStringBuilder_AppendFormat_Streaming ]\n");
    Test_MStringBuilder_AppendFormat_Streaming();

    RUN_TESTS();
}
```

- [ ] **Step 4: 构建 + 跑测试,预期编译失败(`Append`/`AppendByte` 未定义)**

```bash
cmake --build Build --target StringUtilsTest -j4 2>&1 | tail -10
```
预期:`error: 'Append' is not a member of 'MStringBuilder'` / `'AppendByte' was not declared`。

- [ ] **Step 5: 在 `StringUtils.h` 中追加 `Append*` / `AppendByte` / `AppendFormat`**

打开 `Source/Common/Runtime/StringUtils.h`,在 `namespace MStringBuilder` 内现有内容之后追加:

```cpp
    inline void Append(MStringBuilder& Builder, MStringView Text)
    {
        const char* P = Text.data();
        const size_t N = Text.size();
        Builder.Buffer().insert(Builder.Buffer().end(), P, P + N);
    }

    inline void Append(MStringBuilder& Builder, char Ch)
    {
        Builder.Buffer().push_back(static_cast<uint8>(Ch));
    }

    inline void Append(MStringBuilder& Builder, const char* CStr)
    {
        assert(CStr != nullptr);
        Append(Builder, MStringView(CStr));
    }

    inline void Append(MStringBuilder& Builder, const MString& Str)
    {
        Append(Builder, MStringView(Str));
    }

    inline void AppendByte(MStringBuilder& Builder, uint8 Byte)
    {
        Builder.Buffer().push_back(Byte);
    }

    template <typename... TArgs>
    void AppendFormat(MStringBuilder& Builder, MStringView Fmt, TArgs&&... Args)
    {
        fmt::format_to(
            std::back_inserter(Builder.Buffer()),
            Fmt,
            std::forward<TArgs>(Args)...);
    }
} // namespace MStringBuilder
```

> `assert` 来自 `<cassert>`;`std::back_inserter` 来自 `<iterator>`。两者在 `MLib.h` 中可能未直接包含 — 若编译报缺,加 `#include <cassert>` 与 `#include <iterator>` 到 `StringUtils.h` 顶部。

- [ ] **Step 6: 跑全部测试,预期通过**

```bash
cmake --build Build --target StringUtilsTest -j4 && ./Bin/StringUtilsTest
```
预期:8 个用例全通过;退出码 0。

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "feat(string-utils): add MStringBuilder::Append*/AppendByte/AppendFormat

Append(MStringView/char/cstr/MString) + AppendByte + AppendFormat
全部为 namespace MStringBuilder 自由函数,Builder 为第一引用参数,
不返回引用。AppendFormat 走 fmt::format_to + std::back_inserter 直写
TByteArray,无中间 string 分配。"
```

---

### Task 6: 重构 `MStringUtil::ToString` 6 个重载为单模板

**Files:**
- Modify: `Source/Common/Runtime/StringUtils.h`(line 23-46 区域)

**为什么单独任务:** 这是 StringUtils.h 自身的 ToString 重写 — 是本次 PR 中"示范性迁移"的第一项,与新增 API 无关,但在 `MFormat` 上线后立即统一收口能减少后续 PR 的工作量。

- [ ] **Step 1: 删除 6 个 `MStringUtil::ToString` 重载**

打开 `Source/Common/Runtime/StringUtils.h`,定位到 line 19-46:

```cpp
// 项目内字符串工具：统一入口，避免散落 std::to_string / 手写 trim
namespace MStringUtil
{
// 数值转 FString（项目封装，后续可统一字节序/格式）
inline MString ToString(int32 Value) ...
inline MString ToString(int64 Value) ...
inline MString ToString(uint32 Value) ...
inline MString ToString(uint64 Value) ...
inline MString ToString(float Value) ...
inline MString ToString(double Value) ...
```

改为:

```cpp
// 项目内字符串工具：统一入口，避免散落 std::to_string / 手写 trim
namespace MStringUtil
{
// 数值 / 字符串转 MString — 委托 MFormat::ToString,保留旧重载的 6 个调用形式
template <typename T>
inline MString ToString(T Value)
{
    return MFormat::ToString(Value);
}
```

> **签名兼容验证:** 调用 `MStringUtil::ToString(int32(42))` → `T = int32` → `MFormat::ToString(int32(42))` → `fmt::format("{}", 42)` → `"42"`。6 个原重载对应的类型(int32/int64/uint32/uint64/float/double)都能 `std::is_arithmetic_v` 命中,行为等价。

- [ ] **Step 2: 跑 StringUtilsTest,预期通过**

```bash
cmake --build Build --target StringUtilsTest -j4 && ./Bin/StringUtilsTest
```

- [ ] **Step 3: 跑全量构建,预期通过**

```bash
cmake --build Build -j4 2>&1 | tail -20
```
预期:`Build succeeded`;无编译错误(因为 `MStringUtil::ToString` 调用点遍布项目,如果签名不兼容会立即报错)。

- [ ] **Step 4: 在 StringUtilsTest 中加 1 个调用 MStringUtil::ToString 的覆盖**

打开 `TestStringUtilsFormat.cpp`,追加:

```cpp
TEST_CASE(MStringUtil_ToString_DelegatesToMFormat)
{
    EXPECT_EQ(MStringUtil::ToString(int32(42)), MString("42"));
    EXPECT_EQ(MStringUtil::ToString(double64(2.5)), MString("2.5"));
}
```

并在 `StringUtilsTestMain.cpp` 加上对应 `printf` + `Test_*` 调用。

- [ ] **Step 5: 跑 StringUtilsTest,预期全通过**

```bash
cmake --build Build --target StringUtilsTest -j4 && ./Bin/StringUtilsTest
```

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "refactor(string-utils): collapse MStringUtil::ToString to single template

6 个重载(int32/int64/uint32/uint64/float/double)合并为单模板
template<typename T>,内部委托 MFormat::ToString。调用点签名完全
兼容,业务代码无需修改。"
```

---

### Task 7: 迁移 `Json.h` 的 `snprintf("\\u%04x", ...)` → `MFormat::Format`

**Files:**
- Modify: `Source/Common/Runtime/Json.h`(line 308 区域)

- [ ] **Step 1: 打开 `Json.h` 定位转义块**

打开 `/root/Mession/Source/Common/Runtime/Json.h`,定位到 `EscapeString` 函数内部(line 305-313 区域):

```cpp
default:
    if (static_cast<unsigned char>(C) < 0x20)
    {
        char Buf[7];
        std::snprintf(Buf, sizeof(Buf), "\\u%04x", C & 0xff);
        Out += Buf;
    }
    else
    {
        Out.push_back(C);
    }
    break;
```

- [ ] **Step 2: 替换为 `MFormat::Format`**

改为:

```cpp
default:
    if (static_cast<unsigned char>(C) < 0x20)
    {
        Out += MFormat::Format("\\u{:04x}", static_cast<unsigned char>(C));
    }
    else
    {
        Out.push_back(C);
    }
    break;
```

`MFormat` 由 `Json.h` 通过 `#include "Common/Runtime/StringUtils.h"`(若已有)获得;若未包含,加 `#include "Common/Runtime/StringUtils.h"` 到文件顶部 include 块。

- [ ] **Step 3: 跑全量构建,预期通过**

```bash
cmake --build Build -j4 2>&1 | tail -20
```
预期:无编译错误。

- [ ] **Step 4: 跑 StringUtilsTest 与 LogTest(确认 Json 序列化链路无回归)**

```bash
cmake --build Build --target StringUtilsTest LogTest -j4 && \
    ./Bin/StringUtilsTest && \
    ./Bin/LogTest
```
预期:两者全通过。

- [ ] **Step 5: Commit**

```bash
git add Source/Common/Runtime/Json.h
git commit -m "refactor(json): migrate \\u%04x escape snprintf to MFormat::Format"
```

---

### Task 8: 迁移 `Log/CoredumpSink.cpp` 2 处 `snprintf` → `MFormat::Format`

**Files:**
- Modify: `Source/Common/Runtime/Log/CoredumpSink.cpp`(line 111 + line 188)

- [ ] **Step 1: 替换 line 111 的 `\\u%04x` 转义**

打开 `Source/Common/Runtime/Log/CoredumpSink.cpp`,定位:

```cpp
std::snprintf(Esc, sizeof(Esc), "\\u%04x", C);
```

改为:

```cpp
const MString Esc = MFormat::Format("\\u{:04x}", static_cast<unsigned char>(C));
```

同时检查该处后续使用方式(`WriteBatch` 写到 sink 时如何用 `Esc` 字符串)。如果是直接 memcpy 到 sink buffer,把 `Esc.c_str()` 替换即可。**这一步需读上下文确认调用点**;若涉及 sink buffer 写逻辑,改为用 `MStringBuilder::Append` 或在写入处直接构造字符串。

- [ ] **Step 2: 替换 line 188 的 `Pid` 格式化**

打开同一文件,定位:

```cpp
std::snprintf(Pid, sizeof(Pid), "%d", static_cast<int>(::getpid()));
```

改为:

```cpp
const MString Pid = MFormat::Format("{}", static_cast<int>(::getpid()));
```

并相应调整后续使用点(同 Step 1 上下文检查)。

- [ ] **Step 3: 跑全量构建 + LogTest,预期通过**

```bash
cmake --build Build -j4 2>&1 | tail -20 && \
    ./Bin/LogTest
```

- [ ] **Step 4: Commit**

```bash
git add Source/Common/Runtime/Log/CoredumpSink.cpp
git commit -m "refactor(log): migrate CoredumpSink snprintf to MFormat::Format

2 处 snprintf(\\u%04x 字符转义、%d 进程 PID)替换为 MFormat::Format。"
```

---

### Task 9: 迁移 `Log/ConsoleSink.cpp` ISO-8601 时间戳 `snprintf` → `MFormat::Format`

**Files:**
- Modify: `Source/Common/Runtime/Log/ConsoleSink.cpp`(line 41-55 区域)

**特殊点:** 此处用 fmt 的 `chrono` formatter `{:%FT%T}.{:%S}Z` 替代手写 `%04d-%02d-%02dT%02d:%02d:%02d.%03dZ`,**需要确认 fmt 是否支持 chrono**。fmt 11 默认不开启 chrono,需在 `CMakeLists.txt` 中给 fmt 设置 `FMT_USE_CHRONO=ON`(若未设置则退回到手写模式)。

- [ ] **Step 1: 检查当前 fmt 链接是否启用 chrono**

```bash
grep -r "FMT_USE_CHRONO\|chrono" /root/Mession/Build/_deps/fmt-src/include/fmt/chrono.h 2>/dev/null | head -5
```
若文件存在,说明 chrono 可用。

- [ ] **Step 2: 修改 `ConsoleSink.cpp` 的时间戳构造**

打开 `Source/Common/Runtime/Log/ConsoleSink.cpp`,定位到 line 35-56 区域(时间戳格式化函数 `FormatTimestamp` 或类似),把:

```cpp
char FmtBuf[64];
std::snprintf(FmtBuf, sizeof(FmtBuf),
    "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
    Tm.tm_year + 1900, Tm.tm_mon + 1, Tm.tm_mday,
    Tm.tm_hour, Tm.tm_min, Tm.tm_sec, Millis);
const size_t Len = std::strlen(FmtBuf);
if (BufferSize > 0)
{
    const size_t Copy = (Len >= BufferSize) ? (BufferSize - 1) : Len;
    std::memcpy(Buffer, FmtBuf, Copy);
    Buffer[Copy] = '\0';
}
```

改为:

```cpp
const MString FmtBuf = MFormat::Format(
    "{:04d}-{:02d}-{:02d}T{:02d}:{:02d}:{:02d}.{:03d}Z",
    Tm.tm_year + 1900, Tm.tm_mon + 1, Tm.tm_mday,
    Tm.tm_hour, Tm.tm_min, Tm.tm_sec, Millis);
if (BufferSize > 0)
{
    const size_t Copy = (FmtBuf.size() >= BufferSize) ? (BufferSize - 1) : FmtBuf.size();
    std::memcpy(Buffer, FmtBuf.data(), Copy);
    Buffer[Copy] = '\0';
}
```

> 这里**保留手写数字格式**(不切 chrono),最小化风险;chrono 切换作为后续单独 PR。

- [ ] **Step 3: 跑全量构建 + LogTest,预期通过**

```bash
cmake --build Build -j4 2>&1 | tail -20 && ./Bin/LogTest
```

- [ ] **Step 4: Commit**

```bash
git add Source/Common/Runtime/Log/ConsoleSink.cpp
git commit -m "refactor(log): migrate ConsoleSink ISO-8601 snprintf to MFormat::Format"
```

---

### Task 10: 迁移 `Log/RollingFileSink.cpp` 5 处 `snprintf` → `MFormat::Format`

**Files:**
- Modify: `Source/Common/Runtime/Log/RollingFileSink.cpp`(line 51 / 146 / 157 / 188 / 263)

- [ ] **Step 1: 替换 line 51(ISO-8601 时间戳)**

与 Task 9 Step 2 同样的方式,但本文件用 `Tm` 结构体不同的字段名。读上下文后,把 `std::snprintf` 块替换为:

```cpp
const MString FmtBuf = MFormat::Format(
    "{:04d}-{:02d}-{:02d}T{:02d}:{:02d}:{:02d}.{:03d}Z",
    Tm.tm_year + 1900, Tm.tm_mon + 1, Tm.tm_mday,
    Tm.tm_hour, Tm.tm_min, Tm.tm_sec, Millis);
const size_t Len = FmtBuf.size();
if (BufferSize > 0)
{
    const size_t Copy = (Len >= BufferSize) ? (BufferSize - 1) : Len;
    std::memcpy(Buffer, FmtBuf.data(), Copy);
    Buffer[Copy] = '\0';
}
```

- [ ] **Step 2: 替换 line 146 (`"%s.%d"` 文件名)**

```cpp
char Candidate[256];
std::snprintf(Candidate, sizeof(Candidate), "%s.%d", FilePath.c_str(), N);
```
→
```cpp
const MString Candidate = MFormat::Format("{}.{}", FilePath, N);
```

- [ ] **Step 3: 替换 line 157 (`"%s.%d"` 归档文件名)**

```cpp
char ArchivePath[256];
std::snprintf(ArchivePath, sizeof(ArchivePath), "%s.%d", FilePath.c_str(), N);
```
→
```cpp
const MString ArchivePath = MFormat::Format("{}.{}", FilePath, N);
```

- [ ] **Step 4: 替换 line 188(`"%s.%d"` 重命名路径)**

```cpp
char Old[256];
std::snprintf(Old, sizeof(Old), "%s.%d", FilePath.c_str(), N);
```
→
```cpp
const MString Old = MFormat::Format("{}.{}", FilePath, N);
```

- [ ] **Step 5: 替换 line 263(JSON header 的 snprintf)**

读上下文(此 `snprintf` 在循环内写 JSON header `Cursor` 指针),用 `MStringBuilder::AppendFormat` 替代:

```cpp
const int HeaderN = std::snprintf(Cursor, static_cast<size_t>(BufEnd - Cursor), "{\"ts\":\"%s\",\"lvl\":\"%s\",...", ...);
```

改为(需要先在函数外声明 `MStringBuilder` 或用临时 builder):

```cpp
// 在函数顶部或合适位置声明
MStringBuilder HeaderBuilder;
MStringBuilder::Reserve(HeaderBuilder, 512);
MStringBuilder::AppendFormat(HeaderBuilder, "{{\"ts\":\"{}\",\"lvl\":\"{}\",...", Ts, Lvl, ...); // fmt 中 { 需转义为 {{

const MString& HeaderStr = ...; // 取出来
const size_t Copy = std::min(HeaderStr.size(), static_cast<size_t>(BufEnd - Cursor));
std::memcpy(Cursor, HeaderStr.data(), Copy);
Cursor += Copy;
```

**注意:** fmt 中 `{` 需写 `{{`,`}` 需写 `}}`。具体转义方式读上下文后调整。

- [ ] **Step 6: 跑全量构建 + LogTest,预期通过**

```bash
cmake --build Build -j4 2>&1 | tail -30 && ./Bin/LogTest
```

- [ ] **Step 7: Commit**

```bash
git add Source/Common/Runtime/Log/RollingFileSink.cpp
git commit -m "refactor(log): migrate RollingFileSink 5 snprintf to MFormat::Format

替换 ISO-8601 时间戳、文件路径拼接、JSON header 写入 共 5 处
std::snprintf;循环内的 header 写入改用 MStringBuilder::AppendFormat
(避开双层 { } 转义 + 复用 buffer)。"
```

---

### Task 11: 迁移 `Log/Log.cpp` `vsnprintf` → `fmt::vformat_to`(变参路径)

**Files:**
- Modify: `Source/Common/Runtime/Log/Log.cpp`(line 57-69 `FormatInline` 函数)

**为什么独立任务:** 这是项目中**唯一的 `va_list` 路径**,fmt 处理 `va_list` 需要 `fmt::vformat_to` 或 `fmt::make_format_args` 配合,不能简单替换为 `MFormat::Format`(后者是编译期 args)。

- [ ] **Step 1: 读 `FormatInline` 函数当前签名**

打开 `Source/Common/Runtime/Log/Log.cpp` line 57-69:

```cpp
size_t FormatInline(const char* Fmt, va_list Args, char* Out, size_t OutSize)
{
    if (OutSize == 0) return 0;
    const int N = std::vsnprintf(Out, OutSize, Fmt, Args);
    if (N < 0) { Out[0] = '\0'; return 0; }
    const size_t Len = static_cast<size_t>(N);
    if (Len >= OutSize) return OutSize - 1;
    return Len;
}
```

调用点(行内 grep 验证)在 Log.cpp 内某处用 `va_list` 把 caller 的 variadic args 传进来。

- [ ] **Step 2: 重写 `FormatInline` 用 fmt**

```cpp
size_t FormatInline(const char* Fmt, va_list Args, char* Out, size_t OutSize)
{
    if (OutSize == 0) return 0;
    try
    {
        // fmt::vformat 需要 fmt::basic_format_args<fmt::format_context>;
        // 但 va_list 路径的标准做法是 fmt::vformat_to + 手动构建 args。
        // 这里用 fmt::vformat 把结果写入 MString 再 memcpy(变参路径无法
        // 直接 fmt::vformat_to,因为 args 类型不可知)。
        const MString Result = fmt::vformat(
            Fmt,
            fmt::make_format_args(Args));  // ← 这条路径 fmt 不直接支持 va_list
        const size_t Len = Result.size();
        if (Len >= OutSize)
        {
            std::memcpy(Out, Result.data(), OutSize - 1);
            Out[OutSize - 1] = '\0';
            return OutSize - 1;
        }
        std::memcpy(Out, Result.data(), Len);
        Out[Len] = '\0';
        return Len;
    }
    catch (const fmt::format_error&)
    {
        Out[0] = '\0';
        return 0;
    }
}
```

**问题:** `fmt::make_format_args` 不直接接 `va_list`。需要查 fmt 文档 — **可能需要保留 `std::vsnprintf`**,或换 `fmt::format_string` 的 runtime parse 路径(`fmt::runtime(...)`)。这是 fmt 在 C++17 下的固有限制。

**实际可行方案:** 保留 `std::vsnprintf` 调用,在 caller 侧改用 `MFormat::Format` 的"已知 args"路径;`FormatInline` 内部维持 vsnprintf,但加上注释说明为何此处不用 fmt。

打开函数,改为:

```cpp
size_t FormatInline(const char* Fmt, va_list Args, char* Out, size_t OutSize)
{
    // va_list 路径:fmt 在 C++17 下无直接 va_list → format_args 的官方 API。
    // fmt::make_format_args 需要编译期参数类型,vsnprintf 是此路径上
    // 最简实现。后续若 fmt 提供 va_list 支持(0.x 或 1.x 实验分支)
    // 再切换。
    if (OutSize == 0) return 0;
    const int N = std::vsnprintf(Out, OutSize, Fmt, Args);
    if (N < 0) { Out[0] = '\0'; return 0; }
    const size_t Len = static_cast<size_t>(N);
    if (Len >= OutSize) return OutSize - 1;
    return Len;
}
```

**决策:** 接受保留 `vsnprintf`,但加注释说明。变参路径的 fmt 切换作为后续 PR 单独跟进。

- [ ] **Step 3: 在 caller 侧(caller of FormatInline)尝试切换**

读 `Log.cpp` 中调用 `FormatInline` 的位置。如果 caller 是已知 args(例如 `Log::Write(ELogLevel::Info, "x={}", x)` 这类宏展开),把 caller 改为直接 `MFormat::Format("x={}", x)` 写到固定 buffer,绕开 `va_list`。

> 如果 caller 真的把 `...` 透传给 `FormatInline`,本次保留 vsnprintf;不要强行换实现。

- [ ] **Step 4: 跑全量构建 + LogTest,预期通过**

```bash
cmake --build Build -j4 2>&1 | tail -20 && ./Bin/LogTest
```

- [ ] **Step 5: Commit**

```bash
git add Source/Common/Runtime/Log/Log.cpp
git commit -m "refactor(log): annotate FormatInline va_list path; defer fmt migration

va_list → fmt::format_args 在 C++17 下无官方 API,保留 std::vsnprintf
但加注释说明限制。caller 侧的已知 args 路径已在本次其它任务中
切换到 MFormat::Format。"
```

---

### Task 12: 全量回归

**Files:**
- 无新文件,纯验证

- [ ] **Step 1: 完整重建 + 跑所有 unit test**

```bash
cmake --build Build -j4 2>&1 | tail -30
./Bin/StringUtilsTest
./Bin/LogTest
./Bin/MAsyncTest
./Bin/InnerTypeTest
./Bin/AwaitableTest
```
预期:全绿。

- [ ] **Step 2: 跑 verify_protocol(协议层无回归)**

```bash
python3 Scripts/verify_protocol.py
```
预期:通过。本次未改 RPC schema,理论上无影响。

- [ ] **Step 3: 跑 validate 链(端到端)**

```bash
python3 Scripts/validate.py --build-dir Build --no-build
```
预期:客户端链路走通(若历史上 flakiness,见 CLAUDE.md "Validation strategy" 备注)。

- [ ] **Step 4: Commit(若有任何微调)**

```bash
git add -A
git commit -m "test(integration): full regression sweep across StringUtils/Log/Protocol"
```

---

## 兼容性矩阵

| 调用点 | 旧 API | 新 API | 状态 |
|---|---|---|---|
| `MStringUtil::ToString(int32 v)` | `inline MString ToString(int32)` | `template<typename T> inline MString ToString(T)` 委托 `MFormat::ToString` | 签名兼容(Task 6) |
| `MStringUtil::TrimInPlace/TrimCopy/Split/Join` | 不变 | 不变 | 不动 |
| `MStringView::TrimView/StartsWith/EndsWith/Contains` | `TStringView` | `MStringView`(别名重命名) | Task 1 已统一 |
| `std::snprintf` 在 Log/Json | 6 处 | `MFormat::Format` | Task 7-10 |
| `std::vsnprintf` 在 Log.cpp | 1 处 | 保留 + 注释 | Task 11 |

## 不在本次范围

- 自定义 fmt formatter(MString utf-8 强制、ProtocolBuffer 类型等) — 后续 PR。
- MHeaderTool 内部 `ostringstream` 迁移 — 后续 PR。
- 其余业务 `std::to_string` 迁移(共 35 处) — 后续 PR。
- std::format 路径 / C++20 std::format — 后续评估(项目固定 C++17)。
- fmt chrono formatter(`{:%FT%T}.{:%S}Z`) 切换 — 后续 PR(本计划保留手写数字格式以最小化风险)。
- va_list → fmt::format_args 路径 — 后续 PR(fmt 在 C++17 下无官方 API)。