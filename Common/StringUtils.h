#pragma once

#include "Core/NetCore.h"
#include <string>

// 项目内字符串工具：统一入口，避免散落 std::to_string / 手写 trim
namespace MString
{
// 数值转 FString（项目封装，后续可统一字节序/格式）
inline FString ToString(int32 Value)
{
    return std::to_string(Value);
}
inline FString ToString(int64 Value)
{
    return std::to_string(Value);
}
inline FString ToString(uint32 Value)
{
    return std::to_string(Value);
}
inline FString ToString(uint64 Value)
{
    return std::to_string(Value);
}
inline FString ToString(float Value)
{
    return std::to_string(Value);
}
inline FString ToString(double Value)
{
    return std::to_string(Value);
}

// 去除首尾空白（不分配新串时修改原串并返回引用）
inline FString& TrimInPlace(FString& Str)
{
    auto Start = Str.find_first_not_of(" \t\r\n");
    if (Start == FString::npos)
    {
        Str.clear();
        return Str;
    }
    auto End = Str.find_last_not_of(" \t\r\n");
    Str = Str.substr(Start, End == FString::npos ? FString::npos : (End - Start + 1));
    return Str;
}

// 返回去除首尾空白后的新串
inline FString TrimCopy(FString Str)
{
    TrimInPlace(Str);
    return Str;
}

// 按单字符分隔拆分，空串返回单元素 [""]
inline TVector<FString> Split(const FString& Str, char Delim)
{
    TVector<FString> Out;
    if (Str.empty())
    {
        Out.push_back(FString());
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
inline FString Join(const TVector<FString>& Parts, char Delim)
{
    if (Parts.empty())
    {
        return FString();
    }
    FString Out = Parts[0];
    for (size_t i = 1; i < Parts.size(); ++i)
    {
        Out += Delim;
        Out += Parts[i];
    }
    return Out;
}
}
