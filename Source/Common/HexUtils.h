#pragma once

#include "Common/MLib.h"

namespace Hex
{
inline MString BytesToHex(const TByteArray& InData)
{
    static const char* Digits = "0123456789ABCDEF";
    MString Out;
    Out.reserve(InData.size() * 2);
    for (uint8 Byte : InData)
    {
        Out.push_back(Digits[(Byte >> 4) & 0x0F]);
        Out.push_back(Digits[Byte & 0x0F]);
    }
    return Out;
}

inline bool TryDecodeHex(const MString& InHex, TByteArray& OutBytes)
{
    OutBytes.clear();
    if (InHex.empty())
    {
        return true;
    }
    if ((InHex.size() % 2) != 0)
    {
        return false;
    }

    auto HexNibble = [](char Ch) -> int32
    {
        if (Ch >= '0' && Ch <= '9')
        {
            return static_cast<int32>(Ch - '0');
        }
        if (Ch >= 'A' && Ch <= 'F')
        {
            return 10 + static_cast<int32>(Ch - 'A');
        }
        if (Ch >= 'a' && Ch <= 'f')
        {
            return 10 + static_cast<int32>(Ch - 'a');
        }
        return -1;
    };

    OutBytes.reserve(InHex.size() / 2);
    for (size_t Index = 0; Index < InHex.size(); Index += 2)
    {
        const int32 Hi = HexNibble(InHex[Index]);
        const int32 Lo = HexNibble(InHex[Index + 1]);
        if (Hi < 0 || Lo < 0)
        {
            OutBytes.clear();
            return false;
        }
        OutBytes.push_back(static_cast<uint8>((Hi << 4) | Lo));
    }
    return true;
}

inline MString BytesToHexString(const uint8* Data, size_t Size)
{
    static const char* HexDigits = "0123456789ABCDEF";
    if (!Data || Size == 0)
    {
        return "";
    }

    MString Result;
    Result.reserve(Size * 2);
    for (size_t Index = 0; Index < Size; ++Index)
    {
        const uint8 Value = Data[Index];
        Result.push_back(HexDigits[(Value >> 4) & 0x0F]);
        Result.push_back(HexDigits[Value & 0x0F]);
    }
    return Result;
}
}
