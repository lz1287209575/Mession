#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Math/Vector.h"
#include "Common/Runtime/Math/Rotator.h"
#include <typeindex>

class MReflectArchive;
struct MJsonValue;

// 属性类型
enum class EPropertyType : uint8
{
    None = 0,
    Int8 = 1,
    Int16 = 2,
    Int32 = 3,
    Int64 = 4,
    UInt8 = 5,
    UInt16 = 6,
    UInt32 = 7,
    UInt64 = 8,
    Float = 9,
    Double = 10,
    Bool = 11,
    String = 12,
    Name = 13,
    Vector = 14,
    Rotator = 15,
    Struct = 16,
    Object = 17,
    Class = 18,
    Enum = 19,
    Array = 20
};

// 属性标志
enum class EPropertyFlags : uint64
{
    None = 0,
    Edit = 1 << 0,
    BlueprintVisible = 1 << 1,
    BlueprintCallable = 1 << 2,
    BlueprintPure = 1 << 3,
    EditConst = 1 << 4,
    Instanced = 1 << 5,
    Export = 1 << 6,
    SaveGame = 1 << 7,
    Replicated = 1 << 8,
    RepSkip = 1 << 9,
    RepNotify = 1 << 10,
    PersistentData = 1 << 11,
    RepToClient = 1 << 12,
    Asset = 1 << 13,
};

// 属性域
enum class EPropertyDomainFlags : uint64
{
    None = 0,
    Replication = 1 << 0,
    Persistence = 1 << 1,
    Asset = 1 << 2,
};

inline constexpr uint64 ToMask(EPropertyFlags Flags)
{
    return static_cast<uint64>(Flags);
}

inline constexpr uint64 ToMask(EPropertyDomainFlags Flags)
{
    return static_cast<uint64>(Flags);
}

inline constexpr bool HasAnyPropertyFlags(EPropertyFlags Value, EPropertyFlags Mask)
{
    return (ToMask(Value) & ToMask(Mask)) != 0;
}

inline constexpr bool HasAnyPropertyDomains(uint64 Value, EPropertyDomainFlags Mask)
{
    return (Value & ToMask(Mask)) != 0;
}

class MProperty
{
public:
    using MutableAccessor = void*(*)(void*);
    using ConstAccessor = const void*(*)(const void*);

    MString Name;
    EPropertyType Type = EPropertyType::None;
    EPropertyFlags Flags = EPropertyFlags::None;
    size_t Offset = 0;
    size_t Size = 0;
    uint16 PropertyId = 0;
    uint32 AssetFieldId = 0;
    uint64 DomainFlags = ToMask(EPropertyDomainFlags::None);
    std::type_index CppTypeIndex = typeid(void);
    MutableAccessor MutableValueAccessor = nullptr;
    ConstAccessor ConstValueAccessor = nullptr;
    TMap<MString, MString> Metadata;

    MProperty() = default;
    MProperty(const MString& InName, EPropertyType InType, size_t InOffset, size_t InSize)
        : Name(InName)
        , Type(InType)
        , Offset(InOffset)
        , Size(InSize)
        , PropertyId(0)
        , AssetFieldId(0)
        , CppTypeIndex(typeid(void))
    {
    }

    MProperty(const MString& InName, EPropertyType InType, size_t InOffset, size_t InSize, const std::type_index& InCppTypeIndex)
        : Name(InName)
        , Type(InType)
        , Flags(EPropertyFlags::None)
        , Offset(InOffset)
        , Size(InSize)
        , PropertyId(0)
        , AssetFieldId(0)
        , CppTypeIndex(InCppTypeIndex)
    {
    }

    MProperty(
        const MString& InName,
        EPropertyType InType,
        size_t InOffset,
        size_t InSize,
        const std::type_index& InCppTypeIndex,
        MutableAccessor InMutableAccessor,
        ConstAccessor InConstAccessor)
        : Name(InName)
        , Type(InType)
        , Flags(EPropertyFlags::None)
        , Offset(InOffset)
        , Size(InSize)
        , PropertyId(0)
        , AssetFieldId(0)
        , CppTypeIndex(InCppTypeIndex)
        , MutableValueAccessor(InMutableAccessor)
        , ConstValueAccessor(InConstAccessor)
    {
    }

    virtual ~MProperty() = default;

    virtual void WriteValue(void* Object, MReflectArchive& Ar) const;
    virtual MString ExportValueToString(const void* Object) const;
    virtual bool ExportJsonValue(const void* Object, MJsonValue& OutValue, MString* OutError = nullptr) const;
    virtual bool ImportJsonValue(void* Object, const MJsonValue& InValue, MString* OutError = nullptr) const;
    virtual bool ExportBinaryValue(const void* Object, TByteArray& OutData, MString* OutError = nullptr) const;
    virtual bool ImportBinaryValue(void* Object, const TByteArray& InData, MString* OutError = nullptr) const;

    template<typename T>
    T* GetValuePtr(void* Object) const
    {
        if (!Object)
        {
            return nullptr;
        }
        if (MutableValueAccessor)
        {
            return static_cast<T*>(MutableValueAccessor(Object));
        }
        return reinterpret_cast<T*>(reinterpret_cast<uint8*>(Object) + Offset);
    }

    template<typename T>
    const T* GetValuePtr(const void* Object) const
    {
        if (!Object)
        {
            return nullptr;
        }
        if (ConstValueAccessor)
        {
            return static_cast<const T*>(ConstValueAccessor(Object));
        }
        return reinterpret_cast<const T*>(reinterpret_cast<const uint8*>(Object) + Offset);
    }

    void* GetValueVoidPtr(void* Object) const
    {
        if (!Object)
        {
            return nullptr;
        }
        if (MutableValueAccessor)
        {
            return MutableValueAccessor(Object);
        }
        return reinterpret_cast<uint8*>(Object) + Offset;
    }

    const void* GetValueVoidPtr(const void* Object) const
    {
        if (!Object)
        {
            return nullptr;
        }
        if (ConstValueAccessor)
        {
            return ConstValueAccessor(Object);
        }
        return reinterpret_cast<const uint8*>(Object) + Offset;
    }

    bool HasAnyFlags(EPropertyFlags InFlags) const
    {
        return HasAnyPropertyFlags(Flags, InFlags);
    }

    bool HasAnyDomains(EPropertyDomainFlags InFlags) const
    {
        return HasAnyPropertyDomains(DomainFlags, InFlags);
    }

    void SetMetadata(const MString& InKey, const MString& InValue)
    {
        Metadata[InKey] = InValue;
    }

    const MString* FindMetadata(const MString& InKey) const
    {
        const auto It = Metadata.find(InKey);
        return (It != Metadata.end()) ? &It->second : nullptr;
    }

    bool HasMetadata(const MString& InKey) const
    {
        return Metadata.find(InKey) != Metadata.end();
    }
};
