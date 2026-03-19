#pragma once

#include "Common/MLib.h"
#include <cstring>
#include <type_traits>
#include <utility>

// 主机字节序（当前行为，保持兼容）
template<typename T>
inline void AppendValue(TByteArray& OutData, const T& Value)
{
    static_assert(std::is_trivially_copyable_v<T>, "AppendValue requires trivially copyable type");

    const size_t WriteOffset = OutData.size();
    OutData.resize(WriteOffset + sizeof(T));
    memcpy(OutData.data() + WriteOffset, &Value, sizeof(T));
}

template<typename T>
inline bool ReadValue(const TByteArray& Data, size_t& Offset, T& OutValue)
{
    static_assert(std::is_trivially_copyable_v<T>, "ReadValue requires trivially copyable type");

    if (Offset + sizeof(T) > Data.size())
    {
        return false;
    }

    memcpy(&OutValue, Data.data() + Offset, sizeof(T));
    Offset += sizeof(T);
    return true;
}

// 网络字节序（大端）：多字节整数应使用以下接口以保证跨平台
inline void AppendValueBE(TByteArray& OutData, uint16 Value)
{
    uint16 Net = HostToNetwork(Value);
    AppendValue(OutData, Net);
}
inline void AppendValueBE(TByteArray& OutData, uint32 Value)
{
    uint32 Net = HostToNetwork(Value);
    AppendValue(OutData, Net);
}
inline void AppendValueBE(TByteArray& OutData, uint64 Value)
{
    uint64 Net = HostToNetwork(Value);
    AppendValue(OutData, Net);
}

inline bool ReadValueBE(const TByteArray& Data, size_t& Offset, uint16& OutValue)
{
    if (!ReadValue(Data, Offset, OutValue))
    {
        return false;
    }
    OutValue = NetworkToHost(OutValue);
    return true;
}
inline bool ReadValueBE(const TByteArray& Data, size_t& Offset, uint32& OutValue)
{
    if (!ReadValue(Data, Offset, OutValue))
    {
        return false;
    }
    OutValue = NetworkToHost(OutValue);
    return true;
}
inline bool ReadValueBE(const TByteArray& Data, size_t& Offset, uint64& OutValue)
{
    if (!ReadValue(Data, Offset, OutValue))
    {
        return false;
    }
    OutValue = NetworkToHost(OutValue);
    return true;
}

inline void AppendString(TByteArray& OutData, const MString& Value)
{
    const uint16 Length = static_cast<uint16>(Value.size());
    AppendValue(OutData, Length);
    if (Length == 0)
    {
        return;
    }

    const size_t WriteOffset = OutData.size();
    OutData.resize(WriteOffset + Length);
    memcpy(OutData.data() + WriteOffset, Value.data(), Length);
}

inline void AppendStringBE(TByteArray& OutData, const MString& Value)
{
    const uint16 Length = static_cast<uint16>(Value.size());
    AppendValueBE(OutData, Length);
    if (Length == 0)
    {
        return;
    }

    const size_t WriteOffset = OutData.size();
    OutData.resize(WriteOffset + Length);
    memcpy(OutData.data() + WriteOffset, Value.data(), Length);
}

inline bool ReadString(const TByteArray& Data, size_t& Offset, MString& OutValue)
{
    uint16 Length = 0;
    if (!ReadValue(Data, Offset, Length) || Offset + Length > Data.size())
    {
        return false;
    }

    OutValue.assign(reinterpret_cast<const char*>(Data.data() + Offset), Length);
    Offset += Length;
    return true;
}

inline bool ReadStringBE(const TByteArray& Data, size_t& Offset, MString& OutValue)
{
    uint16 Length = 0;
    if (!ReadValueBE(Data, Offset, Length) || Offset + Length > Data.size())
    {
        return false;
    }

    OutValue.assign(reinterpret_cast<const char*>(Data.data() + Offset), Length);
    Offset += Length;
    return true;
}

