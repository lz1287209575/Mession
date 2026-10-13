/**
 * @file Property.h
 * @brief 属性反射:属性类型/标志/域枚举 + MProperty 基类(含取值与序列化虚接口)。
 *
 * @detail MProperty 是"字段描述符":记录字段偏移或访问器、类型、标志与元数据,
 *   并把"取值/写值"和"序列化"以虚函数下放给 TProperty<T> 等模板派生类。
 *   属性标志是配置源,真正被序列化使用的"域"(DomainFlags)由
 *   MClass::RegisterProperty 从标志推导,不要在别处手工设置。
 */
#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Runtime/Math/Rotator.h"
#include "Common/Runtime/Math/Vector.h"
#include <typeindex>

class MReflectArchive;
struct MJsonValue;

/**
 * @brief EPropertyType - 字段的运行时类型标签,决定 MProperty::WriteValue 的分支。
 *
 * @detail 该枚举的数值是 .mobj 字段记录里的 "EncodedType" 字节(见
 *   MObjectAssetBinary::EncodeFieldRecords),读取时会和反射类型逐一比对,
 *   因此数值本身属于资产 ABI,不能重排。
 *   当前 WriteValue 只实现到 Struct/Enum;Name/Object/Class/Array 已声明但
 *   没有对应分支,遇到会走 default 打告警且不写入任何字节。
 */
enum class EPropertyType : uint8 {
    None    = 0,
    Int8    = 1,
    Int16   = 2,
    Int32   = 3,
    Int64   = 4,
    UInt8   = 5,
    UInt16  = 6,
    UInt32  = 7,
    UInt64  = 8,
    Float   = 9,
    Double  = 10,
    Bool    = 11,
    String  = 12,
    Name    = 13, // 已声明,当前无序列化分支
    Vector  = 14,
    Rotator = 15,
    Struct  = 16,
    Object  = 17, // 已声明,当前无序列化分支
    Class   = 18, // 已声明,当前无序列化分支
    Enum    = 19,
    Array   = 20 // 已声明,当前无序列化分支(容器走 MVectorProperty)
};

/**
 * @brief EPropertyFlags - 属性标志位。
 *
 * @detail 被运行期真正消费的只有以下几个:
 *   - Replicated / RepNotify / RepToClient → 推导 Replication 域
 *   - SaveGame / PersistentData → 推导 Persistence 域
 *   - Asset → 推导 Asset 域(决定该字段是否进 .mobj 与 JSON 资产)
 *   - Instanced → JSON 导入时允许该字段承载嵌套对象指针
 *   其余位目前只作声明性标注(供生成器/编辑器使用)。
 */
enum class EPropertyFlags : uint64 {
    None              = 0,
    Edit              = 1 << 0,
    BlueprintVisible  = 1 << 1,
    BlueprintCallable = 1 << 2,
    BlueprintPure     = 1 << 3,
    EditConst         = 1 << 4,
    Instanced         = 1 << 5, // 该字段是内嵌子对象指针,导入时需实例化
    Export            = 1 << 6,
    SaveGame          = 1 << 7,
    Replicated        = 1 << 8,
    RepSkip           = 1 << 9,
    RepNotify         = 1 << 10,
    PersistentData    = 1 << 11,
    RepToClient       = 1 << 12,
    Asset             = 1 << 13, // 进资产视图(.mobj / 资产 JSON)
};

/**
 * @brief EPropertyDomainFlags - 属性所属的数据域,用于按域做快照。
 *
 * @detail 域由 EPropertyFlags 推导(MClass::RegisterProperty),一个属性可以同时
 *   属于多个域;按域序列化时只处理 (DomainFlags & Mask) != 0 的属性。
 */
enum class EPropertyDomainFlags : uint64 {
    None        = 0,
    Replication = 1 << 0, // 复制/同步
    Persistence = 1 << 1, // 存档
    Asset       = 1 << 2, // 资产
};

inline constexpr uint64 ToMask(EPropertyFlags Flags) {
    return static_cast<uint64>(Flags);
}

inline constexpr uint64 ToMask(EPropertyDomainFlags Flags) {
    return static_cast<uint64>(Flags);
}

/** @brief 位相交判定:Value 与 Mask 至少有一位重合即为 true。 */
inline constexpr bool HasAnyPropertyFlags(EPropertyFlags Value, EPropertyFlags Mask) {
    return (ToMask(Value) & ToMask(Mask)) != 0;
}

/** @brief 域位相交判定;Value 是已 ToMask 的裸位集合。 */
inline constexpr bool HasAnyPropertyDomains(uint64 Value, EPropertyDomainFlags Mask) {
    return (Value & ToMask(Mask)) != 0;
}

class MProperty {
    public:
    using MutableAccessor = void* (*)(void*);
    using ConstAccessor   = const void* (*)(const void*);

    MString                Name;
    EPropertyType          Type                 = EPropertyType::None;
    EPropertyFlags         Flags                = EPropertyFlags::None;
    size_t                 Offset               = 0;
    size_t                 Size                 = 0;
    uint16                 PropertyId           = 0;
    uint32                 AssetFieldId         = 0;
    uint64                 DomainFlags          = ToMask(EPropertyDomainFlags::None);
    std::type_index        CppTypeIndex         = typeid(void);
    MutableAccessor        MutableValueAccessor = nullptr;
    ConstAccessor          ConstValueAccessor   = nullptr;
    TMap<MString, MString> Metadata;

    MProperty() = default;
    MProperty(const MString& InName, EPropertyType InType, size_t InOffset, size_t InSize) : Name(InName), Type(InType), Offset(InOffset), Size(InSize), PropertyId(0), AssetFieldId(0), CppTypeIndex(typeid(void)) {
    }

    MProperty(const MString& InName, EPropertyType InType, size_t InOffset, size_t InSize, const std::type_index& InCppTypeIndex)
        : Name(InName), Type(InType), Flags(EPropertyFlags::None), Offset(InOffset), Size(InSize), PropertyId(0), AssetFieldId(0), CppTypeIndex(InCppTypeIndex) {
    }

    MProperty(const MString& InName, EPropertyType InType, size_t InOffset, size_t InSize, const std::type_index& InCppTypeIndex, MutableAccessor InMutableAccessor, ConstAccessor InConstAccessor)
        : Name(InName), Type(InType), Flags(EPropertyFlags::None), Offset(InOffset), Size(InSize), PropertyId(0), AssetFieldId(0), CppTypeIndex(InCppTypeIndex), MutableValueAccessor(InMutableAccessor), ConstValueAccessor(InConstAccessor) {
    }

    virtual ~MProperty() = default;

    virtual void    WriteValue(void* Object, MReflectArchive& Ar) const;
    virtual MString ExportValueToString(const void* Object) const;
    virtual bool    ExportJsonValue(const void* Object, MJsonValue& OutValue, MString* OutError = nullptr) const;
    virtual bool    ImportJsonValue(void* Object, const MJsonValue& InValue, MString* OutError = nullptr) const;
    virtual bool    ExportBinaryValue(const void* Object, TByteArray& OutData, MString* OutError = nullptr) const;
    virtual bool    ImportBinaryValue(void* Object, const TByteArray& InData, MString* OutError = nullptr) const;

    /**
     * @brief SetValueFromString - 把命令行/配置里的字符串写入字段值(CLI 解析用)。
     * @param Object   目标对象(基址)
     * @param Value    待解析的字符串
     * @param OutError 失败时写入原因
     * @return true 解析并写入成功;false 表示本类型不支持字符串赋值
     *
     * @detail 基类默认返回 false(不支持);TProperty<T> 按 T 特化,
     *   目前覆盖 MString、各整型、bool、枚举,以及 TVector<T> 容器。
     */
    virtual bool SetValueFromString(void* /*Object*/, const MString& /*Value*/, MString* /*OutError*/ = nullptr) const {
        return false;
    }

    template <typename T> T* GetValuePtr(void* Object) const {
        if (!Object) {
            return nullptr;
        }
        if (MutableValueAccessor) {
            return static_cast<T*>(MutableValueAccessor(Object));
        }
        return reinterpret_cast<T*>(reinterpret_cast<uint8*>(Object) + Offset);
    }

    template <typename T> const T* GetValuePtr(const void* Object) const {
        if (!Object) {
            return nullptr;
        }
        if (ConstValueAccessor) {
            return static_cast<const T*>(ConstValueAccessor(Object));
        }
        return reinterpret_cast<const T*>(reinterpret_cast<const uint8*>(Object) + Offset);
    }

    void* GetValueVoidPtr(void* Object) const {
        if (!Object) {
            return nullptr;
        }
        if (MutableValueAccessor) {
            return MutableValueAccessor(Object);
        }
        return reinterpret_cast<uint8*>(Object) + Offset;
    }

    const void* GetValueVoidPtr(const void* Object) const {
        if (!Object) {
            return nullptr;
        }
        if (ConstValueAccessor) {
            return ConstValueAccessor(Object);
        }
        return reinterpret_cast<const uint8*>(Object) + Offset;
    }

    bool HasAnyFlags(EPropertyFlags InFlags) const {
        return HasAnyPropertyFlags(Flags, InFlags);
    }

    bool HasAnyDomains(EPropertyDomainFlags InFlags) const {
        return HasAnyPropertyDomains(DomainFlags, InFlags);
    }

    void SetMetadata(const MString& InKey, const MString& InValue) {
        Metadata[InKey] = InValue;
    }

    const MString* FindMetadata(const MString& InKey) const {
        const auto It = Metadata.find(InKey);
        return (It != Metadata.end()) ? &It->second : nullptr;
    }

    bool HasMetadata(const MString& InKey) const {
        return Metadata.find(InKey) != Metadata.end();
    }
};
