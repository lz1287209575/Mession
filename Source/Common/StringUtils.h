#pragma once

#include "Common/MLib.h"
#include <string>
#if __cplusplus >= 201703L
#include <string_view>
#endif

#if defined(_WIN32) || defined(_WIN64)
#include <Windows.h>
#endif

// 项目内字符串工具：统一入口，避免散落 std::to_string / 手写 trim
namespace MString
{
// 数值转 FString（项目封装，后续可统一字节序/格式）
inline MString ToString(int32 Value)
{
    return std::to_string(Value);
}
inline MString ToString(int64 Value)
{
    return std::to_string(Value);
}
inline MString ToString(uint32 Value)
{
    return std::to_string(Value);
}
inline MString ToString(uint64 Value)
{
    return std::to_string(Value);
}
inline MString ToString(float Value)
{
    return std::to_string(Value);
}
inline MString ToString(double Value)
{
    return std::to_string(Value);
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

#if __cplusplus >= 201703L
// TStringView 工具：只读视图上的 Trim/转换/前后缀判断（不分配时用 View）
namespace MStringView
{
inline TStringView TrimView(TStringView View)
{
    const char* Whitespace = " \t\r\n";
    auto Start = View.find_first_not_of(Whitespace);
    if (Start == TStringView::npos)
    {
        return TStringView();
    }
    auto End = View.find_last_not_of(Whitespace);
    return View.substr(Start, End == TStringView::npos ? TStringView::npos : (End - Start + 1));
}

inline MString ToFString(TStringView View)
{
    return MString(View);
}

inline bool StartsWith(TStringView View, TStringView Prefix)
{
    return View.size() >= Prefix.size() &&
           View.compare(0, Prefix.size(), Prefix) == 0;
}

inline bool EndsWith(TStringView View, TStringView Suffix)
{
    return View.size() >= Suffix.size() &&
           View.compare(View.size() - Suffix.size(), Suffix.size(), Suffix) == 0;
}

inline bool Contains(TStringView View, TStringView Needle)
{
    return View.find(Needle) != TStringView::npos;
}
}
#endif

#if defined(_WIN32) || defined(_WIN64)
bool WriteUtf8LineToWindowsConsole(const MString& Line)
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
