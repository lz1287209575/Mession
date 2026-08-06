#pragma once

#include "Common/Runtime/MLib.h"
#include <cassert>
#include <iterator>
#include <string>
#if MESSION_CPLUSPLUS >= 201703L
#include <string_view>
#endif

#if defined(_WIN32) || defined(_WIN64)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
#include <Windows.h>
#endif

// --- fmt 薄包装 -------------------------------------------------------------
#include <fmt/format.h>

namespace MFormat
{
// 主入口 — 替代 std::to_string / snprintf / ostringstream 拼装
template <typename... TArgs>
MString Format(MStringView Fmt, const TArgs&... Args)
{
    // const lvalue 引用: rvalue 实参自动延长生命周期至调用结束,lvalue 直接绑定。
    // 比 TTuple + std::apply 简得多,代价为零。
    return fmt::vformat(Fmt, fmt::make_format_args(Args...));
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

// --- MStringBuilder: TByteArray 后端的流式构造器 --------------------------------
// 注意: 类与同名 namespace 不能共存于同一作用域(硬 C++ 错误),故 Reserve/Clear/
// ToString 作为类静态成员实现 — 调用语法 `MStringBuilder::Reserve(B, n)` 与 brief
// 字面声明的自由函数调用完全一致。
class MStringBuilder
{
public:
    MStringBuilder() = default;
    explicit MStringBuilder(size_t InitialCapacity)
    {
        Buf.reserve(InitialCapacity);
    }

    size_t      Size()     const noexcept { return Buf.size(); }
    bool        Empty()    const noexcept { return Buf.empty(); }
    size_t      Capacity() const noexcept { return Buf.capacity(); }
    MStringView View()     const noexcept
    {
        return MStringView(reinterpret_cast<const char*>(Buf.data()), Buf.size());
    }

    // 仅供 fmt::format_to(std::back_inserter(...)) 与外部算法访问
    TByteArray&       Buffer()       noexcept { return Buf; }
    const TByteArray& Buffer() const noexcept { return Buf; }

    static void Reserve(MStringBuilder& Builder, size_t Capacity)
    {
        Builder.Buf.reserve(Capacity);
    }

    static void Clear(MStringBuilder& Builder) noexcept
    {
        Builder.Buf.clear();
    }

    static MString ToString(const MStringBuilder& Builder)
    {
        return MString(reinterpret_cast<const char*>(Builder.Buf.data()), Builder.Buf.size());
    }

    static void Append(MStringBuilder& Builder, MStringView Text)
    {
        const char* P = Text.data();
        const size_t N = Text.size();
        Builder.Buffer().insert(Builder.Buffer().end(), P, P + N);
    }

    static void Append(MStringBuilder& Builder, char Ch)
    {
        Builder.Buffer().push_back(static_cast<uint8>(Ch));
    }

    static void Append(MStringBuilder& Builder, const char* CStr)
    {
        assert(CStr != nullptr);
        Append(Builder, MStringView(CStr));
    }

    static void Append(MStringBuilder& Builder, const MString& Str)
    {
        Append(Builder, MStringView(Str));
    }

    static void AppendByte(MStringBuilder& Builder, uint8 Byte)
    {
        Builder.Buffer().push_back(Byte);
    }

    template <typename... TArgs>
    static void AppendFormat(MStringBuilder& Builder, MStringView Fmt, TArgs&&... Args)
    {
        fmt::format_to(
            std::back_inserter(Builder.Buffer()),
            Fmt,
            std::forward<TArgs>(Args)...);
    }

private:
    TByteArray Buf;
};

// 项目内字符串工具：统一入口，避免散落 std::to_string / 手写 trim
namespace MStringUtil
{
// 数值 / 字符串转 MString — 委托 MFormat::ToString,保留旧重载的 6 个调用形式
template <typename T>
inline MString ToString(T Value)
{
    return MFormat::ToString(Value);
}

// 去除首尾空白（不分配新串时修改原串并返回引用）
inline MString& TrimInPlace(MString& Str)
{
    auto Start = Str.find_first_not_of(" \t\r\n");
    if (Start == MString::npos)
    {
        Str.clear();
        return Str;
    }
    auto End = Str.find_last_not_of(" \t\r\n");
    Str = Str.substr(Start, End == MString::npos ? MString::npos : (End - Start + 1));
    return Str;
}

// 返回去除首尾空白后的新串
inline MString TrimCopy(MString Str)
{
    TrimInPlace(Str);
    return Str;
}

// 按单字符分隔拆分，空串返回单元素 [""]
inline TVector<MString> Split(const MString& Str, char Delim)
{
    TVector<MString> Out;
    if (Str.empty())
    {
        Out.push_back(MString());
        return Out;
    }
    size_t Start = 0;
    for (size_t i = 0; i <= Str.size(); ++i)
    {
        if (i == Str.size() || Str[i] == Delim)
        {
            Out.push_back(Str.substr(Start, i - Start));
            Start = i + 1;
        }
    }
    return Out;
}

// 用单字符连接多个串，空列表返回 ""
inline MString Join(const TVector<MString>& Parts, char Delim)
{
    if (Parts.empty())
    {
        return MString();
    }
    MString Out = Parts[0];
    for (size_t i = 1; i < Parts.size(); ++i)
    {
        Out += Delim;
        Out += Parts[i];
    }
    return Out;
}
}

#if MESSION_CPLUSPLUS >= 201703L
// MStringView 工具：只读视图上的 Trim/转换/前后缀判断（不分配时用 View）
namespace MStringViewUtil
{
inline MStringView TrimView(MStringView View)
{
    const char* Whitespace = " \t\r\n";
    auto Start = View.find_first_not_of(Whitespace);
    if (Start == MStringView::npos)
    {
        return MStringView();
    }
    auto End = View.find_last_not_of(Whitespace);
    return View.substr(Start, End == MStringView::npos ? MStringView::npos : (End - Start + 1));
}

inline MString ToFString(MStringView View)
{
    return MString(View);
}

inline bool StartsWith(MStringView View, MStringView Prefix)
{
    return View.size() >= Prefix.size() &&
           View.compare(0, Prefix.size(), Prefix) == 0;
}

inline bool EndsWith(MStringView View, MStringView Suffix)
{
    return View.size() >= Suffix.size() &&
           View.compare(View.size() - Suffix.size(), Suffix.size(), Suffix) == 0;
}

inline bool Contains(MStringView View, MStringView Needle)
{
    return View.find(Needle) != MStringView::npos;
}
}
#endif

#if defined(_WIN32) || defined(_WIN64)
inline bool WriteUtf8LineToWindowsConsole(const MString& Line)
{
    HANDLE StdoutHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (StdoutHandle == nullptr || StdoutHandle == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    DWORD ConsoleMode = 0;
    if (!GetConsoleMode(StdoutHandle, &ConsoleMode))
    {
        return false;
    }

    const int WideLength = MultiByteToWideChar(
        CP_UTF8,
        0,
        Line.c_str(),
        static_cast<int>(Line.size()),
        nullptr,
        0);
    if (WideLength <= 0)
    {
        return false;
    }

    std::wstring WideLine;
    WideLine.resize(static_cast<size_t>(WideLength));
    if (MultiByteToWideChar(
            CP_UTF8,
            0,
            Line.c_str(),
            static_cast<int>(Line.size()),
            WideLine.data(),
            WideLength) <= 0)
    {
        return false;
    }

    WideLine += L"\n";
    DWORD CharsWritten = 0;
    return WriteConsoleW(
        StdoutHandle,
        WideLine.c_str(),
        static_cast<DWORD>(WideLine.size()),
        &CharsWritten,
        nullptr) != 0;
}
#endif
