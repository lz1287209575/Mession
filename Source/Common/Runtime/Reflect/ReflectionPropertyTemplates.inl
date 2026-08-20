// ============================================
// 属性模板：默认行为 + 容器特化
// ============================================

#include "Common/Runtime/Json.h"
#include "Common/Runtime/Asset/MObjectAssetBinary.h"

#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <limits>

namespace MObjectAssetJson
{
bool ExportStructToJsonValue(const MClass* StructMeta, const void* StructData, MJsonValue& OutValue, MString* OutError);
bool ImportStructFromJsonValue(const MClass* StructMeta, void* StructData, const MJsonValue& InValue, MString* OutError);
bool ExportAssetObjectToJsonValue(const MObject* Object, MJsonValue& OutValue, MString* OutError);
bool ImportAssetObjectFieldsFromJsonValue(MObject* Object, const MJsonValue& InValue, MString* OutError);
}

namespace MObjectAssetBinary
{
bool EncodeStructFields(const MClass* StructMeta, const void* StructData, TByteArray& OutData, MString* OutError);
bool DecodeStructFields(const MClass* StructMeta, void* StructData, const TByteArray& InData, MString* OutError);
}

// 前向声明：用于属性模板特化
template<typename T>
struct TPropertySnapshotOps;

template<typename T>
struct TPropertyStringExporter;

template<typename T>
struct TPropertyJsonExporter;

template<typename T>
struct TPropertyJsonImporter;

template<typename T>
struct TPropertyBinaryExporter;

template<typename T>
struct TPropertyBinaryImporter;

template<typename TValue>
inline MString ReflectValueToString(const TValue& Value);

template<typename TValue>
inline bool ExportReflectValueToJson(const TValue& Value, MJsonValue& OutValue, MString* OutError);

template<typename TValue>
inline bool ImportReflectValueFromJson(const MJsonValue& InValue, TValue& OutValue, const MProperty* Prop, void* OwnerObject, MString* OutError);

template<typename TElement>
inline bool ExportReflectValueToJson(const TVector<TElement>& Value, MJsonValue& OutValue, MString* OutError);

template<typename TElement>
inline bool ImportReflectValueFromJson(
    const MJsonValue& InValue,
    TVector<TElement>& OutValue,
    const MProperty* Prop,
    void* OwnerObject,
    MString* OutError);

template<typename TValue>
inline bool ExportReflectValueToBinary(const TValue& Value, TByteArray& OutData, MString* OutError);

template<typename TValue>
inline bool ImportReflectValueFromBinary(const TByteArray& InData, TValue& OutValue, const MProperty* Prop, void* OwnerObject, MString* OutError);

template<typename TElement>
inline bool ExportReflectValueToBinary(const TVector<TElement>& Value, TByteArray& OutData, MString* OutError);

template<typename TElement>
inline bool ImportReflectValueFromBinary(
    const TByteArray& InData,
    TVector<TElement>& OutValue,
    const MProperty* Prop,
    void* OwnerObject,
    MString* OutError);


// ---------- SetValueFromString 特化（CLI 解析用） ----------

template<typename T> class TProperty;

template<typename T>
struct TPropertyStringImporter
{
    static bool Import(const MProperty* /*Prop*/, void* /*Object*/, const MString& /*Value*/, MString* OutError)
    {
        if (OutError) *OutError = "string_import_unsupported_type";
        return false;
    }
};

template<>
struct TPropertyStringImporter<MString>
{
    static bool Import(const MProperty* Prop, void* Object, const MString& Value, MString* /*OutError*/)
    {
        if (!Prop || !Object) return false;
        auto* Ptr = Prop->GetValuePtr<MString>(Object);
        if (!Ptr) return false;
        *Ptr = Value;
        return true;
    }
};

template<>
struct TPropertyStringImporter<bool>
{
    static bool Import(const MProperty* Prop, void* Object, const MString& Value, MString* OutError)
    {
        if (!Prop || !Object) return false;
        auto* Ptr = Prop->GetValuePtr<bool>(Object);
        if (!Ptr) return false;
        MString Lower = Value;
        for (auto& C : Lower) C = static_cast<char>(std::tolower(static_cast<unsigned char>(C)));
        if (Lower == "true" || Lower == "1" || Lower == "yes" || Lower == "on")
        {
            *Ptr = true;
            return true;
        }
        if (Lower == "false" || Lower == "0" || Lower == "no" || Lower == "off")
        {
            *Ptr = false;
            return true;
        }
        if (OutError) *OutError = "string_import_invalid_bool:" + Value;
        return false;
    }
};

template<typename TInteger>
struct TPropertyStringIntegerImporter
{
    static bool Import(const MProperty* Prop, void* Object, const MString& Value, MString* OutError)
    {
        if (!Prop || !Object) return false;
        auto* Ptr = Prop->GetValuePtr<TInteger>(Object);
        if (!Ptr) return false;
        if (Value.empty())
        {
            if (OutError) *OutError = "string_import_empty_integer";
            return false;
        }
        char* End = nullptr;
        errno = 0;
        unsigned long long Raw = std::strtoull(Value.c_str(), &End, 0);
        if (End == Value.c_str() || (End != nullptr && *End != '\0') || errno != 0)
        {
            if (OutError) *OutError = "string_import_invalid_integer:" + Value;
            return false;
        }
        if (Raw > static_cast<unsigned long long>((std::numeric_limits<TInteger>::max)()))
        {
            if (OutError) *OutError = "string_import_integer_out_of_range:" + Value;
            return false;
        }
        *Ptr = static_cast<TInteger>(Raw);
        return true;
    }
};

template<> struct TPropertyStringImporter<int8>   : TPropertyStringIntegerImporter<int8> {};
template<> struct TPropertyStringImporter<int16>  : TPropertyStringIntegerImporter<int16> {};
template<> struct TPropertyStringImporter<int32>  : TPropertyStringIntegerImporter<int32> {};
template<> struct TPropertyStringImporter<int64>  : TPropertyStringIntegerImporter<int64> {};
template<> struct TPropertyStringImporter<uint8>  : TPropertyStringIntegerImporter<uint8> {};
template<> struct TPropertyStringImporter<uint16> : TPropertyStringIntegerImporter<uint16> {};
template<> struct TPropertyStringImporter<uint32> : TPropertyStringIntegerImporter<uint32> {};
template<> struct TPropertyStringImporter<uint64> : TPropertyStringIntegerImporter<uint64> {};

template<>
struct TPropertyStringImporter<float>
{
    static bool Import(const MProperty* Prop, void* Object, const MString& Value, MString* OutError)
    {
        if (!Prop || !Object) return false;
        auto* Ptr = Prop->GetValuePtr<float>(Object);
        if (!Ptr) return false;
        if (Value.empty())
        {
            if (OutError) *OutError = "string_import_empty_float";
            return false;
        }
        char* End = nullptr;
        errno = 0;
        float V = std::strtof(Value.c_str(), &End);
        if (End == Value.c_str() || (End != nullptr && *End != '\0') || errno != 0)
        {
            if (OutError) *OutError = "string_import_invalid_float:" + Value;
            return false;
        }
        *Ptr = V;
        return true;
    }
};

template<>
struct TPropertyStringImporter<double>
{
    static bool Import(const MProperty* Prop, void* Object, const MString& Value, MString* OutError)
    {
        if (!Prop || !Object) return false;
        auto* Ptr = Prop->GetValuePtr<double>(Object);
        if (!Ptr) return false;
        if (Value.empty())
        {
            if (OutError) *OutError = "string_import_empty_float";
            return false;
        }
        char* End = nullptr;
        errno = 0;
        double V = std::strtod(Value.c_str(), &End);
        if (End == Value.c_str() || (End != nullptr && *End != '\0') || errno != 0)
        {
            if (OutError) *OutError = "string_import_invalid_float:" + Value;
            return false;
        }
        *Ptr = V;
        return true;
    }
};

// MSTRUCT aggregate string importer: parses a compact "Type@addr:port" form
// into a struct that exposes a static ImportFromCompactString method. Used for
// nested MSTRUCT element types in TVector fields (e.g. SEchoServiceConfig::Peers
// is TVector<SServicePeerConfig> with MSTRUCT annotation). The reflection system
// uses this specialization when TPropertyStringImporter<TVector<TElement>>
// recurses into TElement::SetValueFromString at ReflectionPropertyTemplates.inl:253.
template<typename TAggregate>
struct TPropertyStringImporterAggregate
{
    static bool Import(const MProperty* Prop, void* Object, const MString& Value, MString* OutError)
    {
        if (!Prop || !Object) return false;
        auto* Ptr = Prop->template GetValuePtr<TAggregate>(Object);
        if (!Ptr) return false;
        if (!TAggregate::ImportFromCompactString(*Ptr, Value))
        {
            if (OutError) *OutError = "string_import_aggregate_parse_failed:" + Value;
            return false;
        }
        return true;
    }
};

// cpp17 replacement for `if constexpr (requires { T::ImportFromCompactString; })`
// overload-resolution dispatch between the aggregate-importer path (when
// `&T::ImportFromCompactString` is well-formed) and the per-type importer
// fallback. Specializations come in matching pairs (primary + fallback).
template<typename T, typename = void>
struct TPropertySetValueFromStringDispatch
{
    static bool Call(const MProperty* Prop, void* Object, const MString& Value, MString* OutError)
    {
        return TPropertyStringImporter<T>::Import(Prop, Object, Value, OutError);
    }
};

template<typename T>
struct TPropertySetValueFromStringDispatch<T, std::enable_if_t<std::is_member_object_pointer_v<decltype(&T::ImportFromCompactString)>>>
{
    static bool Call(const MProperty* Prop, void* Object, const MString& Value, MString* OutError)
    {
        return TPropertyStringImporterAggregate<T>::Import(Prop, Object, Value, OutError);
    }
};

template<typename TElement>
struct TPropertyStringImporter<TVector<TElement>>
{
    static bool Import(const MProperty* Prop, void* Object, const MString& Value, MString* OutError)
    {
        if (!Prop || !Object) return false;
        auto* Vec = Prop->GetValuePtr<TVector<TElement>>(Object);
        if (!Vec) return false;
        Vec->clear();

        size_t Pos = 0;
        while (Pos <= Value.size())
        {
            size_t Comma = Value.find(',', Pos);
            MString Token = (Comma == MString::npos)
                ? Value.substr(Pos)
                : Value.substr(Pos, Comma - Pos);
            while (!Token.empty() && std::isspace(static_cast<unsigned char>(Token.front()))) Token.erase(Token.begin());
            while (!Token.empty() && std::isspace(static_cast<unsigned char>(Token.back()))) Token.pop_back();

            if (!Token.empty())
            {
                TElement E{};
                TProperty<TElement> ElementProp(MString(), EPropertyType::None, 0, sizeof(TElement), EPropertyFlags::None);
                MString ElemErr;
                if (!ElementProp.SetValueFromString(&E, Token, &ElemErr))
                {
                    if (OutError) *OutError = "string_import_array_element:" + ElemErr;
                    return false;
                }
                Vec->push_back(std::move(E));
            }
            if (Comma == MString::npos) break;
            Pos = Comma + 1;
        }
        return true;
    }
};

template<typename T>
class TProperty : public MProperty
{
public:
    TProperty(const MString& InName, EPropertyType InType, size_t InOffset, size_t InSize, EPropertyFlags InFlags)
        : MProperty(InName, InType, InOffset, InSize, std::type_index(typeid(T)))
    {
        Flags = InFlags;
    }

    TProperty(
        const MString& InName,
        EPropertyType InType,
        size_t InOffset,
        size_t InSize,
        EPropertyFlags InFlags,
        MutableAccessor InMutableAccessor,
        ConstAccessor InConstAccessor)
        : MProperty(InName, InType, InOffset, InSize, std::type_index(typeid(T)), InMutableAccessor, InConstAccessor)
    {
        Flags = InFlags;
    }

    virtual void WriteValue(void* Object, MReflectArchive& Ar) const override
    {
        // 统一递归序列化(替代 TPropertySnapshotOps→基类 WriteBytes 浅拷贝):
        // 嵌套 MSTRUCT/vector/MString 字段按内容序列化,而非拷贝内部指针
        // (浅拷贝曾导致读回后与源对象共享 vector 缓冲 → 析构 double free)。
        if (!Object)
        {
            return;
        }
        T* ValuePtr = GetValuePtr<T>(Object);
        if (!ValuePtr)
        {
            return;
        }
        SerializeArchiveValue(Ar, *ValuePtr);
    }

    virtual MString ExportValueToString(const void* Object) const override
    {
        return TPropertyStringExporter<T>::Export(this, Object);
    }

    virtual bool ExportJsonValue(const void* Object, MJsonValue& OutValue, MString* OutError = nullptr) const override
    {
        return TPropertyJsonExporter<T>::Export(this, Object, OutValue, OutError);
    }

    virtual bool ImportJsonValue(void* Object, const MJsonValue& InValue, MString* OutError = nullptr) const override
    {
        return TPropertyJsonImporter<T>::Import(this, Object, InValue, OutError);
    }

    virtual bool ExportBinaryValue(const void* Object, TByteArray& OutData, MString* OutError = nullptr) const override
    {
        return TPropertyBinaryExporter<T>::Export(this, Object, OutData, OutError);
    }

    virtual bool ImportBinaryValue(void* Object, const TByteArray& InData, MString* OutError = nullptr) const override
    {
        return TPropertyBinaryImporter<T>::Import(this, Object, InData, OutError);
    }

    virtual bool SetValueFromString(void* Object, const MString& Value, MString* OutError = nullptr) const override
    {
        // Prefer MSTRUCT aggregate importer (which calls T::ImportFromCompactString)
        // when the type is a marked aggregate. Otherwise fall back to the per-type
        // POD importer.
        // cpp17: overload resolution on `&T::ImportFromCompactString` (was
        // `if constexpr (requires { T::ImportFromCompactString; })` under C++20).
        return TPropertySetValueFromStringDispatch<T>::Call(this, Object, Value, OutError);
    }
};


template<typename TObject, typename TValue, TValue TObject::* MemberPtr>
class TMemberProperty : public TProperty<TValue>
{
public:
    TMemberProperty(const MString& InName, EPropertyType InType, EPropertyFlags InFlags)
        : TProperty<TValue>(
            InName,
            InType,
            0,
            sizeof(TValue),
            InFlags,
            &TMemberProperty::GetMutableValue,
            &TMemberProperty::GetConstValue)
    {
    }

private:
    static void* GetMutableValue(void* Object)
    {
        return &(static_cast<TObject*>(Object)->*MemberPtr);
    }

    static const void* GetConstValue(const void* Object)
    {
        return &(static_cast<const TObject*>(Object)->*MemberPtr);
    }
};

template<typename TValue>
class TOffsetProperty : public TProperty<TValue>
{
public:
    TOffsetProperty(const MString& InName, EPropertyType InType, size_t InOffset, EPropertyFlags InFlags)
        : TProperty<TValue>(InName, InType, InOffset, sizeof(TValue), InFlags)
    {
    }
};

template<typename TValue>
inline MProperty* CreateOffsetProperty(const MString& InName, EPropertyType InType, size_t InOffset, EPropertyFlags InFlags = EPropertyFlags::None)
{
    return new TOffsetProperty<TValue>(InName, InType, InOffset, InFlags);
}

// 默认序列化：退回到 MProperty 的基础实现
template<typename T>
struct TPropertySnapshotOps
{
    static void Apply(const MProperty* Prop, void* Object, MReflectArchive& Ar)
    {
        if (!Prop)
        {
            return;
        }
        const_cast<MProperty*>(Prop)->MProperty::WriteValue(Object, Ar);
    }
};

template<typename T>
struct TPropertyStringExporter
{
    static MString Export(const MProperty* Prop, const void* Object)
    {
        if (!Prop)
        {
            return "<null-prop>";
        }
        return Prop->MProperty::ExportValueToString(Object);
    }
};

template<typename T>
struct TPropertyJsonExporter
{
    static bool Export(const MProperty* Prop, const void* Object, MJsonValue& OutValue, MString* OutError)
    {
        if (!Prop || !Object)
        {
            if (OutError)
            {
                *OutError = "json_export_invalid_object";
            }
            return false;
        }

        const T* ValuePtr = Prop->GetValuePtr<T>(Object);
        if (!ValuePtr)
        {
            if (OutError)
            {
                *OutError = "json_export_null_value_ptr:" + Prop->Name;
            }
            return false;
        }

        return ExportReflectValueToJson(*ValuePtr, OutValue, OutError);
    }
};

template<typename T>
struct TPropertyJsonImporter
{
    static bool Import(const MProperty* Prop, void* Object, const MJsonValue& InValue, MString* OutError)
    {
        if (!Prop || !Object)
        {
            if (OutError)
            {
                *OutError = "json_import_invalid_object";
            }
            return false;
        }

        T* ValuePtr = Prop->GetValuePtr<T>(Object);
        if (!ValuePtr)
        {
            if (OutError)
            {
                *OutError = "json_import_null_value_ptr:" + Prop->Name;
            }
            return false;
        }

        return ImportReflectValueFromJson(InValue, *ValuePtr, Prop, Object, OutError);
    }
};

template<typename T>
struct TPropertyBinaryExporter
{
    static bool Export(const MProperty* Prop, const void* Object, TByteArray& OutData, MString* OutError)
    {
        if (!Prop || !Object)
        {
            if (OutError)
            {
                *OutError = "binary_export_invalid_object";
            }
            return false;
        }

        const T* ValuePtr = Prop->GetValuePtr<T>(Object);
        if (!ValuePtr)
        {
            if (OutError)
            {
                *OutError = "binary_export_null_value_ptr:" + Prop->Name;
            }
            return false;
        }

        OutData.clear();
        return ExportReflectValueToBinary(*ValuePtr, OutData, OutError);
    }
};

template<typename T>

// ---- 超大 ReflectionPropertyTemplates.inl 拆分(2026-08-20)----
// 内容:Binary/Json 序列化实现 + 容器特化
#include "Common/Runtime/Reflect/ReflectionPropertyTemplates.BinaryJson.inl"
// 内容:StringExporter 容器特化(TVector/TMap/TSet)
#include "Common/Runtime/Reflect/ReflectionPropertyTemplates.StringExport.inl"
