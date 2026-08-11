# StringUtils: MFormat + MStringBuilder

| | |
|---|---|
| 日期 | 2026-08-05 |
| 范围 | `Source/Common/Runtime/StringUtils.h`、CMake 接入、Log/Json/MLib 迁移 |
| 状态 | 设计草案,待用户审阅 |

## 背景

`MStringUtil::ToString` 当前用 `std::to_string`,无类型安全的格式化能力;Log 模块与 Json 模块散落 `std::snprintf`,多处 `MString += ...` 反复分配。目标是在 `StringUtils.h` 提供两个新工具:

1. `MFormat` — fmt 库的薄包装,统一入口
2. `MStringBuilder` — 内部 `TByteArray` 预 reserve 的流式构造器

## 命名约束

- `MFormat`:`M*` 前缀(类/非模板别名),非模板入口。
- `MStringBuilder`:`M*` 前缀,内含 `TByteArray` 成员。
- 所有 Append* / AppendFormat / Reserve / Clear / ToString 等"对 builder 做一件事"的函数,**全部为 `namespace MStringBuilder` 下的自由函数**,builder 作为第一引用参数传入。**不返回引用**,不 method chaining。
- `MStringView`(`std::string_view` 的项目别名):`M*` 前缀,**因为它不是模板**。同步修正 `MLib.h` 中错误命名为 `TStringView` 的别名,全项目重命名。

## 设计

### 1. MFormat API

```cpp
namespace MFormat
{
    // 主入口 — 替代 std::to_string / snprintf / ostringstream 拼装
    template <typename... TArgs>
    MString Format(MStringView Fmt, TArgs&&... Args);

    // 类型安全数值转字符串 — 替代 MStringUtil::ToString 重载
    template <typename T>
    MString ToString(T Value);
}
```

**实现**:
- `Format`:转 `fmt::vformat(Fmt, fmt::make_format_args(args...))`。
- `ToString<T>`:`if constexpr` 分支 — `std::is_same_v<T, MString>` 直接返回;`std::is_arithmetic_v<T>` 走 `fmt::format("{}", v)`;其他类型走 fmt 通用 `{}`。

**错误**:格式串不合法抛 `fmt::format_error`(沿用 fmt 自身行为,不屏蔽)。

### 2. MStringBuilder API

```cpp
class MStringBuilder
{
public:
    MStringBuilder() = default;
    explicit MStringBuilder(size_t InitialCapacity);

    size_t      Size()     const noexcept { return Buffer.size(); }
    bool        Empty()    const noexcept { return Buffer.empty(); }
    size_t      Capacity() const noexcept { return Buffer.capacity(); }
    MStringView View()     const noexcept;

    // 仅供 fmt::format_to(std::back_inserter(...)) 用
    TByteArray&       Buffer()       noexcept { return Buffer; }
    const TByteArray& Buffer() const noexcept { return Buffer; }

private:
    TByteArray Buffer;
};

namespace MStringBuilder
{
    void Reserve(MStringBuilder& Builder, size_t Capacity);
    void Clear  (MStringBuilder& Builder) noexcept;
    MString ToString(const MStringBuilder& Builder);

    void Append     (MStringBuilder& Builder, MStringView Text);
    void Append     (MStringBuilder& Builder, char Ch);
    void Append     (MStringBuilder& Builder, const char* CStr);
    void Append     (MStringBuilder& Builder, const MString& Str);
    void AppendByte (MStringBuilder& Builder, uint8 Byte);

    template <typename... TArgs>
    void AppendFormat(MStringBuilder& Builder, MStringView Fmt, TArgs&&... Args);
}
```

**AppendFormat 实现**:
```cpp
template <typename... TArgs>
void AppendFormat(MStringBuilder& Builder, MStringView Fmt, TArgs&&... Args)
{
    fmt::format_to(
        std::back_inserter(Builder.Buffer()),
        Fmt,
        std::forward<TArgs>(Args)...);
}
```

> 不预判 fmt 长度,buffer 走 `TByteArray` 自然翻倍 — 这是 fmt 文档推荐的 streaming pattern。

**使用形态**:
```cpp
MStringBuilder B(256);
MStringBuilder::Append(B, "header=");
MStringBuilder::AppendFormat(B, "id={} name={}\n", 42, "alice");
MStringBuilder::AppendByte(B, 0x0A);
MString Out = MStringBuilder::ToString(B);
```

### 3. CMake 接入

`/root/Mession/CMakeLists.txt`,在 `mession_core` 库定义前插入:

```cmake
# --- fmt (FetchContent, header-only) ---------------------------------------
include(FetchContent)
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
set(FMT_HEADER_ONLY ON CACHE BOOL "Use fmt header-only build" FORCE)
```

`mession_core` PUBLIC 链接:
```cmake
target_link_libraries(mession_core
    PUBLIC
        fmt::fmt-header-only
)
```

新增测试 target(仿 `LogTest`):
```cmake
add_executable(StringUtilsTest
    Source/Common/Runtime/Tests/StringUtilsTest.cpp
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

### 4. 迁移范围

**Log 子系统**(4 文件):
- `Source/Common/Runtime/Log/ConsoleSink.cpp` — `std::snprintf(FmtBuf, sizeof(FmtBuf), "%s %s:%d", ...)` → `MFormat::Format(...)`。
- `Source/Common/Runtime/Log/RollingFileSink.cpp` — 2 处 `snprintf` 同上。
- `Source/Common/Runtime/Log/CoredumpSink.cpp` — `snprintf(Esc, ..., "\\u%04x", C)` → `MFormat::Format("\\u{:04x}", C)`。
- `Source/Common/Runtime/Log/Log.cpp` — `std::vsnprintf` → `fmt::vformat_to`。

**Json 转义**:
- `Source/Common/Runtime/Json.h` line 308: `snprintf(Buf, ..., "\\u%04x", C & 0xff)` → `MFormat::Format("\\u{:04x}", C & 0xff)`。

**StringUtils.h 自身**:
- 6 个 `MStringUtil::ToString(...)` 重写为单模板 `MStringUtil::ToString<T>(T)` → 内部 `MFormat::ToString<T>(v)`。`namespace MStringUtil` 保留。

**TStringView → MStringView 全项目重命名**:
- `Source/Common/Runtime/MLib.h`:`using TStringView = std::string_view;` → `using MStringView = std::string_view;`。
- 全项目 grep 替换所有 `TStringView` 为 `MStringView`(估计 < 30 处)。

**不迁移(本次范围外)**:
- `Source/Tools/MHeaderTool/Generation/CodeGenerator.h` 的 `ostringstream` — 构建期工具,后续单独 PR。
- 35 个 `std::to_string` 中的其余业务调用点 — 留作后续 PR;本次只在 StringUtils.h 自身重写 6 个 ToString,作为示范。

### 5. 测试策略

`Source/Common/Runtime/Tests/StringUtilsTest.cpp`(仿 `LogTest` 的 main.cpp 风格),用例:

| 类别 | 覆盖 |
|------|------|
| `MFormat::Format` | `{}` 替换 int/string/double;`{:x}` hex;`{:08x}` 0-pad;浮点精度 |
| `MFormat::Format` 错误 | 格式串不合法 → 抛 `fmt::format_error` |
| `MFormat::ToString<T>` | int32/int64/uint32/uint64/float/double,MString 透传 |
| `MStringBuilder::Reserve` | Capacity ≥ N |
| `MStringBuilder::Append` | 4 种重载(MStringView/char/cstr/MString) |
| `MStringBuilder::AppendFormat` | 与 `MFormat::Format` 结果一致 |
| `MStringBuilder::AppendByte` | 单字节写入 |
| `MStringBuilder::Clear` | Size=0,Capacity 不变 |
| `MStringBuilder::ToString` | 零拷贝构造,内容一致 |
| 跨模块 | 同一 TU 混用 Append + AppendFormat + AppendByte,ToString 校对完整输出 |

**运行**:`cmake --build Build -j4 && ./Bin/StringUtilsTest`。

### 6. 错误处理

| 场景 | 行为 |
|------|------|
| fmt 格式串不合法 | 抛 `fmt::format_error`(异常路径) |
| `Append(CStr)` 收到 nullptr | DEBUG `assert(CStr != nullptr)`,Release 不检查 |
| `Reserve(N)` 巨大 N | `std::bad_alloc` 自然抛出,不再额外检查 |
| `ToString` on empty builder | 返回空 `MString`,不抛 |
| `MFormat::Format` 空 fmt | 返回空 `MString`,不抛 |
| fmt header / 链接缺失 | CMake 配置期失败,显式编译错误 |

## 兼容性

- 现有 `MStringUtil::ToString(int32/int64/uint32/uint64/float/double)` 调用点保持工作(6 个重载由单模板实现,签名兼容)。
- 现有 `MStringUtil::TrimInPlace / TrimCopy / Split / Join` 不变。
- `MStringView::TrimView / StartsWith / EndsWith / Contains` 不变。
- 唯一破坏性变更:`TStringView` → `MStringView`(全项目同步重命名)。

## 不在范围

- 自定义 fmt formatter(MString utf-8 强制、ProtocolBuffer 类型等) — 后续 PR。
- MHeaderTool 内部 `ostringstream` 迁移 — 后续 PR。
- 其余业务 `std::to_string` 迁移 — 后续 PR。
- std::format 路径 / C++20 std::format — 后续评估(本项目固定 C++17)。