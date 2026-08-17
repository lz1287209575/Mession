# StringUtils:字符串工具(MFormat / MStringBuilder / MStringUtil)

项目统一的字符串处理入口,提供类型安全的格式化、零拷贝流式构造,以及常用的 Trim / Split / Join 等工具,替代散落的 `std::to_string`、`std::snprintf` 和反复分配的手拼字符串。

## 总体结构

| 命名空间 / 类 | 职责 |
|---|---|
| `MFormat` | fmt 库薄包装:类型安全格式化 `Format` 与数值转串 `ToString` |
| `MStringBuilder` | 内部 `TByteArray` 预 reserve 的流式构造器,追加不反复分配 |
| `MStringUtil` | 通用字符串工具:`ToString` / `TrimInPlace` / `TrimCopy` / `Split` / `Join` |
| `MStringViewUtil` | 只读 `MStringView` 工具:`TrimView` / `ToFString` / `StartsWith` / `EndsWith` / `Contains`(C++17 起) |

依赖 fmt 库(header-only,11.0.2,经 CMake `FetchContent` 引入)。项目固定 C++17。

## MFormat:格式化入口

```cpp
namespace MFormat
{
    // 主入口 — 替代 std::to_string / snprintf / ostringstream
    template <typename... TArgs>
    MString Format(MStringView Fmt, const TArgs&... Args);   // 内部 fmt::vformat

    // 类型安全数值转字符串,MString 直接透传
    template <typename T>
    MString ToString(T Value);
}
```

- `Format` 为 fmt 格式串:占位符 `{}` 支持数字、字符串、浮点;`{:x}` 十六进制、`{:08x}` 零填充、`{:.<n>}` 浮点精度等 fmt 全量语法。
- `ToString<T>`:`TIsSame<T, MString>` 时原样返回,其余类型走 `fmt::format("{}", v)`。
- 格式串不合法时抛 `fmt::format_error`(沿用 fmt 自身行为,不屏蔽);空格式串返回空 `MString`,不抛。

## MStringBuilder:流式构造器

```cpp
class MStringBuilder
{
public:
    MStringBuilder();                              // 默认构造
    explicit MStringBuilder(size_t InitialCapacity);

    size_t      Size()     const noexcept;
    bool        Empty()    const noexcept;
    size_t      Capacity() const noexcept;
    MStringView View()     const noexcept;         // 零拷贝只读视图
    TByteArray&       Buffer()       noexcept;     // 仅供 fmt::format_to 与外部算法
    const TByteArray& Buffer() const noexcept;

    // 以下操作以静态成员实现,调用语法与自由函数一致:MStringBuilder::Append(B, ...)
    static void    Reserve(MStringBuilder& Builder, size_t Capacity);
    static void    Clear(MStringBuilder& Builder) noexcept;
    static MString ToString(const MStringBuilder& Builder);   // 零拷贝构造 MString

    static void Append(MStringBuilder& Builder, MStringView Text);
    static void Append(MStringBuilder& Builder, char Ch);
    static void Append(MStringBuilder& Builder, const char* CStr);   // DEBUG 下 assert 非空
    static void Append(MStringBuilder& Builder, const MString& Str);
    static void AppendByte(MStringBuilder& Builder, uint8 Byte);

    template <typename... TArgs>
    static void AppendFormat(MStringBuilder& Builder, MStringView Fmt, TArgs&&... Args);
};
```

要点:

- 后端为 `TByteArray`,构造时传入期望容量可一次预分配;追加走 buffer 自然翻倍,不预判 fmt 结果长度(即 fmt 文档推荐的 streaming pattern)。
- 所有 `Append*` / `AppendFormat` / `Reserve` / `Clear` / `ToString` 均为对 builder 做一件事的静态成员函数,builder 作为第一引用参数传入,不返回引用、不做 method chaining。
- 空 builder 上 `ToString` 返回空 `MString`,不抛。
- `Append(CStr)` 收到 nullptr:DEBUG 构建 `assert(CStr != nullptr)`,Release 不检查。
- `Reserve` 巨大容量:`std::bad_alloc` 自然抛出,无额外检查。

## MStringUtil:通用字符串工具

```cpp
namespace MStringUtil
{
    template <typename T>
    MString ToString(T Value);                    // 委托 MFormat::ToString,兼容旧 6 个重载调用形式

    MString& TrimInPlace(MString& Str);           // 去除首尾空白(" \t\r\n"),原地修改并返回引用
    MString  TrimCopy(MString Str);               // 去除首尾空白,返回新串
    TVector<MString> Split(const MString& Str, char Delim);  // 空串返回单元素 [""]
    MString  Join(const TVector<MString>& Parts, char Delim); // 空列表返回 ""
}
```

## MStringViewUtil:只读视图工具

不分配内存的只读操作,用于需要避免拷贝的场景:

```cpp
namespace MStringViewUtil
{
    MStringView TrimView(MStringView View);       // 去除首尾空白,返回子视图
    MString     ToFString(MStringView View);      // 视图转 MString
    bool StartsWith(MStringView View, MStringView Prefix);
    bool EndsWith(MStringView View, MStringView Suffix);
    bool Contains(MStringView View, MStringView Needle);
}
```

## 用法示例

```cpp
// 类型安全格式化
MString Msg = MFormat::Format("id={} name={}\n", 42, "alice");
MString Hex = MFormat::Format("0x{:08x}", 0x2a);       // "0x0000002a"
MString S   = MFormat::ToString(3.14);                 // "3.14"

// 流式构造
MStringBuilder B(256);
MStringBuilder::Append(B, "header=");
MStringBuilder::AppendFormat(B, "id={} name={}\n", 42, "alice");
MStringBuilder::AppendByte(B, 0x0A);
MStringBuilder::Append(B, "END");
MString Out = MStringBuilder::ToString(B);             // 零拷贝取出

// 通用工具
MString T  = MStringUtil::TrimCopy("  hello  ");       // "hello"
TVector<MString> Parts = MStringUtil::Split("a,b,c", ',');
MString J  = MStringUtil::Join(Parts, '-');            // "a-b-c"

// 只读视图
bool Ok = MStringViewUtil::StartsWith("mession://x", "mession");
```

## 错误处理

| 场景 | 行为 |
|---|---|
| fmt 格式串不合法 | 抛 `fmt::format_error` |
| `Append(CStr)` 收到 nullptr | DEBUG `assert`,Release 不检查 |
| `Reserve` 巨大容量 | `std::bad_alloc` 自然抛出 |
| 空 builder `ToString` | 返回空 `MString`,不抛 |
| `MFormat::Format` 空格式串 | 返回空 `MString`,不抛 |
| fmt 头文件 / 链接缺失 | CMake 配置期显式失败 |

## 相关实现

- `Source/Common/Runtime/StringUtils.h` — MFormat / MStringBuilder / MStringUtil / MStringViewUtil 全部实现(header-only)
- `Source/Common/Runtime/MLib.h` — `MString`、`MStringView`(`std::string_view`)、`TByteArray`、`TIsSame` 等项目别名
- `Source/Common/Runtime/Tests/StringUtilsTestMain.cpp` — 单元测试(含 `TestStringUtilsFormat.cpp`),target 名 `StringUtilsTest`
- `CMakeLists.txt` — fmt 依赖接入(FetchContent,`FMT_DIR` 可指本地源码跳过拉取;`FMT_HEADER_ONLY ON`;`mession_core` PUBLIC 链接 `fmt::fmt-header-only`)及 `StringUtilsTest` 测试 target
- `Source/Common/Runtime/Log/ConsoleSink.cpp`、`RollingFileSink.cpp`、`CoredumpSink.cpp`、`Log.cpp` — Log 子系统已迁移至 MFormat / fmt
- `Source/Common/Runtime/Json.h` — JSON 转义(`\u{:04x}`)已迁移至 MFormat
