#pragma once

#include "Common/Runtime/MLib.h"
#include <cstring>
#include <type_traits>

class MObject;
class MClass;

namespace MObjectAssetBinary
{
constexpr uint16 FileVersion = 1;

template<typename TValue, bool = std::is_enum_v<TValue>>
struct TBinaryRawType
{
    using Type = TValue;
};

template<typename TValue>
struct TBinaryRawType<TValue, true>
{
    using Type = std::underlying_type_t<TValue>;
};

enum class EPayloadEncoding : uint32
{
    None = 0,
    JsonBridge = 1,
    TaggedFields = 2,
};

struct SFileHeader
{
    uint16 Version = FileVersion;
    uint16 Flags = 0;
    uint32 RootAssetTypeId = 0;
    EPayloadEncoding PayloadEncoding = EPayloadEncoding::None;
    uint32 PayloadSize = 0;
};

inline void AppendByte(TByteArray& OutBytes, uint8 Value)
{
    OutBytes.push_back(Value);
}

template<typename TValue>
inline void AppendFixedLE(TByteArray& OutBytes, TValue Value)
{
    static_assert(std::is_integral_v<TValue> || std::is_enum_v<TValue>, "AppendFixedLE expects integral or enum");
    using TRaw = typename TBinaryRawType<TValue>::Type;
    const TRaw RawValue = static_cast<TRaw>(Value);
    for (size_t Index = 0; Index < sizeof(TRaw); ++Index)
    {
        AppendByte(OutBytes, static_cast<uint8>((static_cast<uint64>(RawValue) >> (Index * 8)) & 0xFFu));
    }
}

inline void AppendFloat32LE(TByteArray& OutBytes, float Value)
{
    uint32 Raw = 0;
    static_assert(sizeof(float) == sizeof(uint32));
    std::memcpy(&Raw, &Value, sizeof(float));
    AppendFixedLE(OutBytes, Raw);
}

inline void AppendFloat64LE(TByteArray& OutBytes, double Value)
{
    uint64 Raw = 0;
    static_assert(sizeof(double) == sizeof(uint64));
    std::memcpy(&Raw, &Value, sizeof(double));
    AppendFixedLE(OutBytes, Raw);
}

inline bool ReadByte(const TByteArray& InBytes, size_t& InOutOffset, uint8& OutValue)
{
    if (InOutOffset >= InBytes.size())
    {
        return false;
    }
    OutValue = InBytes[InOutOffset++];
    return true;
}

template<typename TValue>
inline bool ReadFixedLE(const TByteArray& InBytes, size_t& InOutOffset, TValue& OutValue)
{
    static_assert(std::is_integral_v<TValue> || std::is_enum_v<TValue>, "ReadFixedLE expects integral or enum");
    using TRaw = typename TBinaryRawType<TValue>::Type;
    if (InOutOffset + sizeof(TRaw) > InBytes.size())
    {
        return false;
    }

    std::make_unsigned_t<TRaw> RawValue = 0;
    for (size_t Index = 0; Index < sizeof(TRaw); ++Index)
    {
        RawValue |= static_cast<std::make_unsigned_t<TRaw>>(InBytes[InOutOffset + Index]) << (Index * 8);
    }
    InOutOffset += sizeof(TRaw);
    OutValue = static_cast<TValue>(static_cast<TRaw>(RawValue));
    return true;
}

inline bool ReadFloat32LE(const TByteArray& InBytes, size_t& InOutOffset, float& OutValue)
{
    uint32 Raw = 0;
    if (!ReadFixedLE(InBytes, InOutOffset, Raw))
    {
        return false;
    }
    std::memcpy(&OutValue, &Raw, sizeof(float));
    return true;
}

inline bool ReadFloat64LE(const TByteArray& InBytes, size_t& InOutOffset, double& OutValue)
{
    uint64 Raw = 0;
    if (!ReadFixedLE(InBytes, InOutOffset, Raw))
    {
        return false;
    }
    std::memcpy(&OutValue, &Raw, sizeof(double));
    return true;
}

inline void AppendVarUInt(TByteArray& OutBytes, uint64 Value)
{
    while (Value >= 0x80u)
    {
        AppendByte(OutBytes, static_cast<uint8>((Value & 0x7Fu) | 0x80u));
        Value >>= 7;
    }
    AppendByte(OutBytes, static_cast<uint8>(Value));
}

inline bool ReadVarUInt(const TByteArray& InBytes, size_t& InOutOffset, uint64& OutValue)
{
    OutValue = 0;
    uint32 Shift = 0;
    while (Shift < 64)
    {
        uint8 Byte = 0;
        if (!ReadByte(InBytes, InOutOffset, Byte))
        {
            return false;
        }

        OutValue |= static_cast<uint64>(Byte & 0x7Fu) << Shift;
        if ((Byte & 0x80u) == 0)
        {
            return true;
        }
        Shift += 7;
    }
    return false;
}

inline void AppendString(TByteArray& OutBytes, const MString& Value)
{
    AppendVarUInt(OutBytes, static_cast<uint64>(Value.size()));
    OutBytes.insert(OutBytes.end(), Value.begin(), Value.end());
}

inline bool ReadString(const TByteArray& InBytes, size_t& InOutOffset, MString& OutValue)
{
    uint64 Length = 0;
    if (!ReadVarUInt(InBytes, InOutOffset, Length))
    {
        return false;
    }
    if (Length > static_cast<uint64>(InBytes.size() - InOutOffset))
    {
        return false;
    }

    OutValue.assign(
        reinterpret_cast<const char*>(InBytes.data() + InOutOffset),
        static_cast<size_t>(Length));
    InOutOffset += static_cast<size_t>(Length);
    return true;
}

bool EncodeStructFields(const MClass* StructMeta, const void* StructData, TByteArray& OutData, MString* OutError = nullptr);
bool DecodeStructFields(const MClass* StructMeta, void* StructData, const TByteArray& InData, MString* OutError = nullptr);

bool BuildFromObject(const MObject* RootObject, TByteArray& OutBytes, MString* OutError = nullptr);
bool BuildFromJson(const MString& JsonText, TByteArray& OutBytes, MString* OutError = nullptr);

MObject* LoadFromBytes(const TByteArray& Bytes, MObject* Outer = nullptr, MString* OutError = nullptr);
bool ReadHeader(const TByteArray& Bytes, SFileHeader& OutHeader, size_t& OutPayloadOffset, MString* OutError = nullptr);
bool ExtractPayloadJson(const TByteArray& Bytes, MString& OutJson, SFileHeader* OutHeader = nullptr, MString* OutError = nullptr);
}
