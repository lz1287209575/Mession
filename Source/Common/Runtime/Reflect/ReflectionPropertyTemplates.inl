// ============================================
// 属性模板：默认行为 + 容器特化
// ============================================

#include "Common/Runtime/Json.h"
#include "Common/Runtime/Asset/MObjectAssetBinary.h"

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
        TPropertySnapshotOps<T>::Apply(this, Object, Ar);
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
struct TPropertyBinaryImporter
{
    static bool Import(const MProperty* Prop, void* Object, const TByteArray& InData, MString* OutError)
    {
        if (!Prop || !Object)
        {
            if (OutError)
            {
                *OutError = "binary_import_invalid_object";
            }
            return false;
        }

        T* ValuePtr = Prop->GetValuePtr<T>(Object);
        if (!ValuePtr)
        {
            if (OutError)
            {
                *OutError = "binary_import_null_value_ptr:" + Prop->Name;
            }
            return false;
        }

        return ImportReflectValueFromBinary(InData, *ValuePtr, Prop, Object, OutError);
    }
};

namespace Detail
{
inline bool ExportStructLikeValueToJson(
    const MClass* StructMeta,
    const void* StructData,
    MJsonValue& OutValue,
    MString* OutError)
{
    if (!StructMeta)
    {
        if (OutError)
        {
            *OutError = "json_export_missing_struct_meta";
        }
        return false;
    }

    return MObjectAssetJson::ExportStructToJsonValue(StructMeta, StructData, OutValue, OutError);
}

inline bool ImportStructLikeValueFromJson(
    const MClass* StructMeta,
    void* StructData,
    const MJsonValue& InValue,
    MString* OutError)
{
    if (!StructMeta)
    {
        if (OutError)
        {
            *OutError = "json_import_missing_struct_meta";
        }
        return false;
    }

    return MObjectAssetJson::ImportStructFromJsonValue(StructMeta, StructData, InValue, OutError);
}

inline bool ExportStructLikeValueToBinary(
    const MClass* StructMeta,
    const void* StructData,
    TByteArray& OutData,
    MString* OutError)
{
    if (!StructMeta)
    {
        if (OutError)
        {
            *OutError = "binary_export_missing_struct_meta";
        }
        return false;
    }

    return MObjectAssetBinary::EncodeStructFields(StructMeta, StructData, OutData, OutError);
}

inline bool ImportStructLikeValueFromBinary(
    const MClass* StructMeta,
    void* StructData,
    const TByteArray& InData,
    MString* OutError)
{
    if (!StructMeta)
    {
        if (OutError)
        {
            *OutError = "binary_import_missing_struct_meta";
        }
        return false;
    }

    return MObjectAssetBinary::DecodeStructFields(StructMeta, StructData, InData, OutError);
}

inline bool EnsureFullyConsumed(const TByteArray& InData, size_t Offset, MString* OutError)
{
    if (Offset == InData.size())
    {
        return true;
    }

    if (OutError)
    {
        *OutError = "binary_trailing_bytes";
    }
    return false;
}
}

template<typename TValue>
inline bool ExportReflectValueToJson(const TValue& Value, MJsonValue& OutValue, MString* OutError)
{
    using TDecayed = std::remove_cv_t<std::remove_reference_t<TValue>>;
    if constexpr (std::is_same_v<TDecayed, MString> || std::is_same_v<TDecayed, MName>)
    {
        OutValue.Type = EJsonType::String;
        OutValue.StringValue = Value;
        return true;
    }
    else if constexpr (std::is_same_v<TDecayed, bool>)
    {
        OutValue.Type = EJsonType::Boolean;
        OutValue.BoolValue = Value;
        return true;
    }
    else if constexpr (std::is_integral_v<TDecayed>)
    {
        OutValue.Type = EJsonType::Number;
        OutValue.NumberValue = static_cast<double>(Value);
        return true;
    }
    else if constexpr (std::is_floating_point_v<TDecayed>)
    {
        OutValue.Type = EJsonType::Number;
        OutValue.NumberValue = static_cast<double>(Value);
        return true;
    }
    else if constexpr (std::is_same_v<TDecayed, SVector>)
    {
        OutValue.Type = EJsonType::Object;
        OutValue.ObjectValue["X"] = MJsonValue{EJsonType::Number, false, static_cast<double>(Value.X)};
        OutValue.ObjectValue["Y"] = MJsonValue{EJsonType::Number, false, static_cast<double>(Value.Y)};
        OutValue.ObjectValue["Z"] = MJsonValue{EJsonType::Number, false, static_cast<double>(Value.Z)};
        return true;
    }
    else if constexpr (std::is_same_v<TDecayed, SRotator>)
    {
        OutValue.Type = EJsonType::Object;
        OutValue.ObjectValue["Pitch"] = MJsonValue{EJsonType::Number, false, static_cast<double>(Value.Pitch)};
        OutValue.ObjectValue["Yaw"] = MJsonValue{EJsonType::Number, false, static_cast<double>(Value.Yaw)};
        OutValue.ObjectValue["Roll"] = MJsonValue{EJsonType::Number, false, static_cast<double>(Value.Roll)};
        return true;
    }
    else if constexpr (std::is_enum_v<TDecayed>)
    {
        using TUnderlying = std::underlying_type_t<TDecayed>;
        if (const MEnum* EnumMeta = MObject::FindEnum(std::type_index(typeid(TDecayed))))
        {
            const int64 EnumValue = static_cast<int64>(static_cast<TUnderlying>(Value));
            if (const MEnumValue* ValueMeta = EnumMeta->FindValueByIntegral(EnumValue))
            {
                OutValue.Type = EJsonType::String;
                OutValue.StringValue = ValueMeta->Name;
                return true;
            }
        }

        OutValue.Type = EJsonType::Number;
        OutValue.NumberValue = static_cast<double>(static_cast<TUnderlying>(Value));
        return true;
    }
    else if constexpr (std::is_pointer_v<TDecayed> &&
                       std::is_base_of_v<MObject, std::remove_pointer_t<TDecayed>>)
    {
        if (!Value)
        {
            OutValue = MJsonValue{};
            return true;
        }

        return MObjectAssetJson::ExportAssetObjectToJsonValue(Value, OutValue, OutError);
    }
    else if constexpr (std::is_same_v<TDecayed, TByteArray>)
    {
        OutValue.Type = EJsonType::Array;
        OutValue.ArrayValue.clear();
        OutValue.ArrayValue.reserve(Value.size());
        for (uint8 Byte : Value)
        {
            MJsonValue Element;
            Element.Type = EJsonType::Number;
            Element.NumberValue = static_cast<double>(Byte);
            OutValue.ArrayValue.push_back(std::move(Element));
        }
        return true;
    }
    else if constexpr (std::is_trivially_copyable_v<TDecayed>)
    {
        if (const MClass* StructMeta = MObject::FindStruct(std::type_index(typeid(TDecayed))))
        {
            return Detail::ExportStructLikeValueToJson(StructMeta, &Value, OutValue, OutError);
        }
        if (OutError)
        {
            *OutError = "json_export_unsupported_trivial_type";
        }
        return false;
    }
    else
    {
        if (const MClass* StructMeta = MObject::FindStruct(std::type_index(typeid(TDecayed))))
        {
            return Detail::ExportStructLikeValueToJson(StructMeta, &Value, OutValue, OutError);
        }
        if (OutError)
        {
            *OutError = "json_export_unsupported_type";
        }
        return false;
    }
}

namespace Detail
{
template<typename TInteger>
inline bool ImportIntegerFromJsonNumber(double NumberValue, TInteger& OutValue, MString* OutError)
{
    if (!std::isfinite(NumberValue))
    {
        if (OutError)
        {
            *OutError = "json_import_non_finite_number";
        }
        return false;
    }

    const double Truncated = std::trunc(NumberValue);
    if (Truncated != NumberValue)
    {
        if (OutError)
        {
            *OutError = "json_import_expected_integer";
        }
        return false;
    }

    if constexpr (std::is_signed_v<TInteger>)
    {
        const double MinValue = static_cast<double>((std::numeric_limits<TInteger>::min)());
        const double MaxValue = static_cast<double>((std::numeric_limits<TInteger>::max)());
        if (NumberValue < MinValue || NumberValue > MaxValue)
        {
            if (OutError)
            {
                *OutError = "json_import_integer_out_of_range";
            }
            return false;
        }
        OutValue = static_cast<TInteger>(static_cast<int64>(NumberValue));
    }
    else
    {
        const double MaxValue = static_cast<double>((std::numeric_limits<TInteger>::max)());
        if (NumberValue < 0.0 || NumberValue > MaxValue)
        {
            if (OutError)
            {
                *OutError = "json_import_integer_out_of_range";
            }
            return false;
        }
        OutValue = static_cast<TInteger>(static_cast<uint64>(NumberValue));
    }
    return true;
}

inline const MJsonValue* FindRequiredObjectMember(
    const MJsonValue& ObjectValue,
    const MString& Key,
    MString* OutError)
{
    if (!ObjectValue.IsObject())
    {
        if (OutError)
        {
            *OutError = "json_import_expected_object";
        }
        return nullptr;
    }

    const auto It = ObjectValue.ObjectValue.find(Key);
    if (It == ObjectValue.ObjectValue.end())
    {
        if (OutError)
        {
            *OutError = "json_import_missing_member:" + Key;
        }
        return nullptr;
    }
    return &It->second;
}
}

template<typename TValue>
inline bool ImportReflectValueFromJson(
    const MJsonValue& InValue,
    TValue& OutValue,
    const MProperty* Prop,
    void* OwnerObject,
    MString* OutError)
{
    using TDecayed = std::remove_cv_t<std::remove_reference_t<TValue>>;
    if constexpr (std::is_same_v<TDecayed, MString> || std::is_same_v<TDecayed, MName>)
    {
        if (!InValue.IsString())
        {
            if (OutError)
            {
                *OutError = "json_import_expected_string";
            }
            return false;
        }
        OutValue = InValue.StringValue;
        return true;
    }
    else if constexpr (std::is_same_v<TDecayed, bool>)
    {
        if (!InValue.IsBool())
        {
            if (OutError)
            {
                *OutError = "json_import_expected_bool";
            }
            return false;
        }
        OutValue = InValue.BoolValue;
        return true;
    }
    else if constexpr (std::is_integral_v<TDecayed>)
    {
        if (!InValue.IsNumber())
        {
            if (OutError)
            {
                *OutError = "json_import_expected_number";
            }
            return false;
        }
        return Detail::ImportIntegerFromJsonNumber(InValue.NumberValue, OutValue, OutError);
    }
    else if constexpr (std::is_floating_point_v<TDecayed>)
    {
        if (!InValue.IsNumber())
        {
            if (OutError)
            {
                *OutError = "json_import_expected_number";
            }
            return false;
        }
        OutValue = static_cast<TDecayed>(InValue.NumberValue);
        return true;
    }
    else if constexpr (std::is_same_v<TDecayed, SVector>)
    {
        const MJsonValue* X = Detail::FindRequiredObjectMember(InValue, "X", OutError);
        const MJsonValue* Y = Detail::FindRequiredObjectMember(InValue, "Y", OutError);
        const MJsonValue* Z = Detail::FindRequiredObjectMember(InValue, "Z", OutError);
        if (!X || !Y || !Z || !X->IsNumber() || !Y->IsNumber() || !Z->IsNumber())
        {
            if (OutError && OutError->empty())
            {
                *OutError = "json_import_invalid_vector";
            }
            return false;
        }
        OutValue.X = static_cast<float>(X->NumberValue);
        OutValue.Y = static_cast<float>(Y->NumberValue);
        OutValue.Z = static_cast<float>(Z->NumberValue);
        return true;
    }
    else if constexpr (std::is_same_v<TDecayed, SRotator>)
    {
        const MJsonValue* Pitch = Detail::FindRequiredObjectMember(InValue, "Pitch", OutError);
        const MJsonValue* Yaw = Detail::FindRequiredObjectMember(InValue, "Yaw", OutError);
        const MJsonValue* Roll = Detail::FindRequiredObjectMember(InValue, "Roll", OutError);
        if (!Pitch || !Yaw || !Roll || !Pitch->IsNumber() || !Yaw->IsNumber() || !Roll->IsNumber())
        {
            if (OutError && OutError->empty())
            {
                *OutError = "json_import_invalid_rotator";
            }
            return false;
        }
        OutValue.Pitch = static_cast<float>(Pitch->NumberValue);
        OutValue.Yaw = static_cast<float>(Yaw->NumberValue);
        OutValue.Roll = static_cast<float>(Roll->NumberValue);
        return true;
    }
    else if constexpr (std::is_enum_v<TDecayed>)
    {
        using TUnderlying = std::underlying_type_t<TDecayed>;
        if (InValue.IsString())
        {
            if (const MEnum* EnumMeta = MObject::FindEnum(std::type_index(typeid(TDecayed))))
            {
                if (const MEnumValue* EnumValue = EnumMeta->FindValue(InValue.StringValue))
                {
                    OutValue = static_cast<TDecayed>(static_cast<TUnderlying>(EnumValue->Value));
                    return true;
                }
            }
            if (OutError)
            {
                *OutError = "json_import_unknown_enum_value";
            }
            return false;
        }
        if (!InValue.IsNumber())
        {
            if (OutError)
            {
                *OutError = "json_import_expected_enum";
            }
            return false;
        }
        TUnderlying RawValue{};
        if (!Detail::ImportIntegerFromJsonNumber(InValue.NumberValue, RawValue, OutError))
        {
            return false;
        }
        OutValue = static_cast<TDecayed>(RawValue);
        return true;
    }
    else if constexpr (std::is_pointer_v<TDecayed> &&
                       std::is_base_of_v<MObject, std::remove_pointer_t<TDecayed>>)
    {
        using TObject = std::remove_pointer_t<TDecayed>;
        if (InValue.IsNull())
        {
            OutValue = nullptr;
            return true;
        }
        if (!InValue.IsObject())
        {
            if (OutError)
            {
                *OutError = "json_import_expected_object_node";
            }
            return false;
        }
        if (!Prop || !Prop->HasAnyFlags(EPropertyFlags::Instanced))
        {
            if (OutError)
            {
                *OutError = "json_import_object_reference_unsupported";
            }
            return false;
        }
        if (!OwnerObject)
        {
            if (OutError)
            {
                *OutError = "json_import_missing_owner_object";
            }
            return false;
        }

        MClass* TargetClass = TObject::StaticClass();
        if (const auto ClassIt = InValue.ObjectValue.find("$class");
            ClassIt != InValue.ObjectValue.end())
        {
            if (!ClassIt->second.IsString())
            {
                if (OutError)
                {
                    *OutError = "json_import_invalid_object_class";
                }
                return false;
            }

            if (ClassIt->second.StringValue != TargetClass->GetName())
            {
                if (OutError)
                {
                    *OutError = "json_import_object_class_mismatch:" + ClassIt->second.StringValue;
                }
                return false;
            }
        }

        auto* NewObject = static_cast<MObject*>(TargetClass->CreateInstance());
        if (!NewObject)
        {
            if (OutError)
            {
                *OutError = "json_import_object_create_failed";
            }
            return false;
        }

        if (const auto NameIt = InValue.ObjectValue.find("$name");
            NameIt != InValue.ObjectValue.end() && NameIt->second.IsString())
        {
            NewObject->SetName(NameIt->second.StringValue);
        }
        NewObject->SetOuter(static_cast<MObject*>(OwnerObject));

        MString NestedError;
        if (!MObjectAssetJson::ImportAssetObjectFieldsFromJsonValue(NewObject, InValue, &NestedError))
        {
            delete NewObject;
            if (OutError)
            {
                *OutError = NestedError;
            }
            return false;
        }

        OutValue = static_cast<TDecayed>(NewObject);
        return true;
    }
    else if constexpr (std::is_same_v<TDecayed, TByteArray>)
    {
        if (!InValue.IsArray())
        {
            if (OutError)
            {
                *OutError = "json_import_expected_array";
            }
            return false;
        }
        OutValue.clear();
        OutValue.reserve(InValue.ArrayValue.size());
        for (const MJsonValue& Element : InValue.ArrayValue)
        {
            uint8 Byte = 0;
            if (!ImportReflectValueFromJson(Element, Byte, Prop, OwnerObject, OutError))
            {
                return false;
            }
            OutValue.push_back(Byte);
        }
        return true;
    }
    else if constexpr (std::is_trivially_copyable_v<TDecayed>)
    {
        if (const MClass* StructMeta = MObject::FindStruct(std::type_index(typeid(TDecayed))))
        {
            return Detail::ImportStructLikeValueFromJson(StructMeta, &OutValue, InValue, OutError);
        }
        if (OutError)
        {
            *OutError = "json_import_unsupported_trivial_type";
        }
        return false;
    }
    else
    {
        if (const MClass* StructMeta = MObject::FindStruct(std::type_index(typeid(TDecayed))))
        {
            return Detail::ImportStructLikeValueFromJson(StructMeta, &OutValue, InValue, OutError);
        }
        if (OutError)
        {
            *OutError = "json_import_unsupported_type";
        }
        return false;
    }
}

template<typename TValue>
inline bool ExportReflectValueToBinary(const TValue& Value, TByteArray& OutData, MString* OutError)
{
    using TDecayed = std::remove_cv_t<std::remove_reference_t<TValue>>;
    OutData.clear();

    if constexpr (std::is_same_v<TDecayed, MString> || std::is_same_v<TDecayed, MName>)
    {
        MObjectAssetBinary::AppendString(OutData, Value);
        return true;
    }
    else if constexpr (std::is_same_v<TDecayed, bool>)
    {
        MObjectAssetBinary::AppendByte(OutData, Value ? 1u : 0u);
        return true;
    }
    else if constexpr (std::is_integral_v<TDecayed>)
    {
        MObjectAssetBinary::AppendFixedLE(OutData, Value);
        return true;
    }
    else if constexpr (std::is_floating_point_v<TDecayed>)
    {
        if constexpr (std::is_same_v<TDecayed, float>)
        {
            MObjectAssetBinary::AppendFloat32LE(OutData, Value);
        }
        else
        {
            MObjectAssetBinary::AppendFloat64LE(OutData, Value);
        }
        return true;
    }
    else if constexpr (std::is_same_v<TDecayed, SVector>)
    {
        MObjectAssetBinary::AppendFloat32LE(OutData, Value.X);
        MObjectAssetBinary::AppendFloat32LE(OutData, Value.Y);
        MObjectAssetBinary::AppendFloat32LE(OutData, Value.Z);
        return true;
    }
    else if constexpr (std::is_same_v<TDecayed, SRotator>)
    {
        MObjectAssetBinary::AppendFloat32LE(OutData, Value.Pitch);
        MObjectAssetBinary::AppendFloat32LE(OutData, Value.Yaw);
        MObjectAssetBinary::AppendFloat32LE(OutData, Value.Roll);
        return true;
    }
    else if constexpr (std::is_enum_v<TDecayed>)
    {
        using TUnderlying = std::underlying_type_t<TDecayed>;
        MObjectAssetBinary::AppendFixedLE(OutData, static_cast<TUnderlying>(Value));
        return true;
    }
    else if constexpr (std::is_pointer_v<TDecayed> &&
                       std::is_base_of_v<MObject, std::remove_pointer_t<TDecayed>>)
    {
        if (OutError)
        {
            *OutError = "binary_export_object_reference_unsupported";
        }
        return false;
    }
    else if constexpr (std::is_same_v<TDecayed, TByteArray>)
    {
        MObjectAssetBinary::AppendVarUInt(OutData, static_cast<uint64>(Value.size()));
        OutData.insert(OutData.end(), Value.begin(), Value.end());
        return true;
    }
    else if constexpr (std::is_trivially_copyable_v<TDecayed>)
    {
        if (const MClass* StructMeta = MObject::FindStruct(std::type_index(typeid(TDecayed))))
        {
            return Detail::ExportStructLikeValueToBinary(StructMeta, &Value, OutData, OutError);
        }
        if (OutError)
        {
            *OutError = "binary_export_unsupported_trivial_type";
        }
        return false;
    }
    else
    {
        if (const MClass* StructMeta = MObject::FindStruct(std::type_index(typeid(TDecayed))))
        {
            return Detail::ExportStructLikeValueToBinary(StructMeta, &Value, OutData, OutError);
        }
        if (OutError)
        {
            *OutError = "binary_export_unsupported_type";
        }
        return false;
    }
}

template<typename TValue>
inline bool ImportReflectValueFromBinary(
    const TByteArray& InData,
    TValue& OutValue,
    const MProperty* Prop,
    void* OwnerObject,
    MString* OutError)
{
    using TDecayed = std::remove_cv_t<std::remove_reference_t<TValue>>;
    size_t Offset = 0;

    if constexpr (std::is_same_v<TDecayed, MString> || std::is_same_v<TDecayed, MName>)
    {
        return MObjectAssetBinary::ReadString(InData, Offset, OutValue) &&
            Detail::EnsureFullyConsumed(InData, Offset, OutError);
    }
    else if constexpr (std::is_same_v<TDecayed, bool>)
    {
        uint8 Raw = 0;
        if (!MObjectAssetBinary::ReadByte(InData, Offset, Raw))
        {
            if (OutError)
            {
                *OutError = "binary_import_expected_bool";
            }
            return false;
        }
        OutValue = (Raw != 0);
        return Detail::EnsureFullyConsumed(InData, Offset, OutError);
    }
    else if constexpr (std::is_integral_v<TDecayed>)
    {
        if (!MObjectAssetBinary::ReadFixedLE(InData, Offset, OutValue))
        {
            if (OutError)
            {
                *OutError = "binary_import_expected_integral";
            }
            return false;
        }
        return Detail::EnsureFullyConsumed(InData, Offset, OutError);
    }
    else if constexpr (std::is_floating_point_v<TDecayed>)
    {
        const bool bOk = [&]() -> bool
        {
            if constexpr (std::is_same_v<TDecayed, float>)
            {
                return MObjectAssetBinary::ReadFloat32LE(InData, Offset, OutValue);
            }
            else
            {
                return MObjectAssetBinary::ReadFloat64LE(InData, Offset, OutValue);
            }
        }();
        if (!bOk)
        {
            if (OutError)
            {
                *OutError = "binary_import_expected_float";
            }
            return false;
        }
        return Detail::EnsureFullyConsumed(InData, Offset, OutError);
    }
    else if constexpr (std::is_same_v<TDecayed, SVector>)
    {
        if (!MObjectAssetBinary::ReadFloat32LE(InData, Offset, OutValue.X) ||
            !MObjectAssetBinary::ReadFloat32LE(InData, Offset, OutValue.Y) ||
            !MObjectAssetBinary::ReadFloat32LE(InData, Offset, OutValue.Z))
        {
            if (OutError)
            {
                *OutError = "binary_import_expected_vector";
            }
            return false;
        }
        return Detail::EnsureFullyConsumed(InData, Offset, OutError);
    }
    else if constexpr (std::is_same_v<TDecayed, SRotator>)
    {
        if (!MObjectAssetBinary::ReadFloat32LE(InData, Offset, OutValue.Pitch) ||
            !MObjectAssetBinary::ReadFloat32LE(InData, Offset, OutValue.Yaw) ||
            !MObjectAssetBinary::ReadFloat32LE(InData, Offset, OutValue.Roll))
        {
            if (OutError)
            {
                *OutError = "binary_import_expected_rotator";
            }
            return false;
        }
        return Detail::EnsureFullyConsumed(InData, Offset, OutError);
    }
    else if constexpr (std::is_enum_v<TDecayed>)
    {
        using TUnderlying = std::underlying_type_t<TDecayed>;
        TUnderlying RawValue{};
        if (!MObjectAssetBinary::ReadFixedLE(InData, Offset, RawValue))
        {
            if (OutError)
            {
                *OutError = "binary_import_expected_enum";
            }
            return false;
        }
        OutValue = static_cast<TDecayed>(RawValue);
        return Detail::EnsureFullyConsumed(InData, Offset, OutError);
    }
    else if constexpr (std::is_pointer_v<TDecayed> &&
                       std::is_base_of_v<MObject, std::remove_pointer_t<TDecayed>>)
    {
        (void)Prop;
        (void)OwnerObject;
        if (OutError)
        {
            *OutError = "binary_import_object_reference_unsupported";
        }
        return false;
    }
    else if constexpr (std::is_same_v<TDecayed, TByteArray>)
    {
        uint64 Count = 0;
        if (!MObjectAssetBinary::ReadVarUInt(InData, Offset, Count))
        {
            if (OutError)
            {
                *OutError = "binary_import_expected_byte_array";
            }
            return false;
        }
        if (Count > static_cast<uint64>(InData.size() - Offset))
        {
            if (OutError)
            {
                *OutError = "binary_import_truncated_byte_array";
            }
            return false;
        }
        OutValue.assign(InData.begin() + static_cast<std::ptrdiff_t>(Offset),
                        InData.begin() + static_cast<std::ptrdiff_t>(Offset + static_cast<size_t>(Count)));
        Offset += static_cast<size_t>(Count);
        return Detail::EnsureFullyConsumed(InData, Offset, OutError);
    }
    else if constexpr (std::is_trivially_copyable_v<TDecayed>)
    {
        if (const MClass* StructMeta = MObject::FindStruct(std::type_index(typeid(TDecayed))))
        {
            return Detail::ImportStructLikeValueFromBinary(StructMeta, &OutValue, InData, OutError);
        }
        if (OutError)
        {
            *OutError = "binary_import_unsupported_trivial_type";
        }
        return false;
    }
    else
    {
        if (const MClass* StructMeta = MObject::FindStruct(std::type_index(typeid(TDecayed))))
        {
            return Detail::ImportStructLikeValueFromBinary(StructMeta, &OutValue, InData, OutError);
        }
        if (OutError)
        {
            *OutError = "binary_import_unsupported_type";
        }
        return false;
    }
}

template<typename TValue>
inline MString ReflectValueToString(const TValue& Value)
{
    using TDecayed = std::remove_cv_t<std::remove_reference_t<TValue>>;
    if constexpr (std::is_same_v<TDecayed, MString>)
    {
        return "\"" + Value + "\"";
    }
    else if constexpr (std::is_same_v<TDecayed, bool>)
    {
        return Value ? "true" : "false";
    }
    else if constexpr (std::is_same_v<TDecayed, SVector>)
    {
        return "{X=" + MStringUtil::ToString(Value.X) +
               ", Y=" + MStringUtil::ToString(Value.Y) +
               ", Z=" + MStringUtil::ToString(Value.Z) + "}";
    }
    else if constexpr (std::is_same_v<TDecayed, SRotator>)
    {
        return "{Pitch=" + MStringUtil::ToString(Value.Pitch) +
               ", Yaw=" + MStringUtil::ToString(Value.Yaw) +
               ", Roll=" + MStringUtil::ToString(Value.Roll) + "}";
    }
    else if constexpr (std::is_base_of_v<MObject, TDecayed>)
    {
        return Value.ToString();
    }
    else if constexpr (std::is_enum_v<TDecayed>)
    {
        using TUnderlying = std::underlying_type_t<TDecayed>;
        if (const MEnum* EnumMeta = MObject::FindEnum(std::type_index(typeid(TDecayed))))
        {
            const int64 EnumValue = static_cast<int64>(static_cast<TUnderlying>(Value));
            if (const MEnumValue* ValueMeta = EnumMeta->FindValueByIntegral(EnumValue))
            {
                return EnumMeta->GetName() + "::" + ValueMeta->Name;
            }
            return EnumMeta->GetName() + "::" + MStringUtil::ToString(EnumValue);
        }
        return MStringUtil::ToString(static_cast<TUnderlying>(Value));
    }
    else if constexpr (std::is_integral_v<TDecayed>)
    {
        if constexpr (std::is_signed_v<TDecayed>)
        {
            return MStringUtil::ToString(static_cast<int64>(Value));
        }
        else
        {
            return MStringUtil::ToString(static_cast<uint64>(Value));
        }
    }
    else if constexpr (std::is_floating_point_v<TDecayed>)
    {
        return MStringUtil::ToString(static_cast<double>(Value));
    }
    else if constexpr (std::is_trivially_copyable_v<TDecayed>)
    {
        const uint8* Bytes = reinterpret_cast<const uint8*>(&Value);
        MString Result = "<struct hex=";
        static const char* HexDigits = "0123456789ABCDEF";
        for (size_t Index = 0; Index < sizeof(TDecayed); ++Index)
        {
            const uint8 Byte = Bytes[Index];
            Result.push_back(HexDigits[(Byte >> 4) & 0x0F]);
            Result.push_back(HexDigits[Byte & 0x0F]);
        }
        Result += ">";
        return Result;
    }
    else
    {
        return "<unsupported>";
    }
}

// TVector 容器专用序列化
template<typename TElement>
struct TPropertySnapshotOps<TVector<TElement>>
{
    static void Apply(const MProperty* Prop, void* Object, MReflectArchive& Ar)
    {
        if (!Prop || !Object)
        {
            return;
        }

        auto* Vec = Prop->GetValuePtr<TVector<TElement>>(Object);
        if (!Vec)
        {
            return;
        }
        uint32 Count = 0;
        if (Ar.bWriting)
        {
            Count = static_cast<uint32>(Vec->size());
        }

        Ar << Count;

        if (Ar.bReading)
        {
            Vec->resize(Count);
        }

        if (Count == 0)
        {
            return;
        }

        if constexpr (std::is_trivially_copyable_v<TElement>)
        {
            // POD 元素：按字节批量序列化整个数组
            Ar.WriteBytes(Vec->data(), sizeof(TElement) * Count);
        }
        else
        {
            // 非 POD：逐个元素走各自的 operator<<
            for (uint32 Index = 0; Index < Count; ++Index)
            {
                TElement& Element = (*Vec)[Index];
                Ar << Element;
            }
        }
    }
};

template<typename TElement>
struct TPropertyStringExporter<TVector<TElement>>
{
    static MString Export(const MProperty* Prop, const void* Object)
    {
        if (!Prop || !Object)
        {
            return "<null-array>";
        }

        const auto* Vec = Prop->GetValuePtr<TVector<TElement>>(Object);
        if (!Vec)
        {
            return "<null-array>";
        }

        MString Result = "[";
        for (size_t Index = 0; Index < Vec->size(); ++Index)
        {
            if (Index > 0)
            {
                Result += ", ";
            }
            Result += ReflectValueToString((*Vec)[Index]);
        }
        Result += "]";
        return Result;
    }
};

template<typename TElement>
inline bool ExportReflectValueToJson(const TVector<TElement>& Value, MJsonValue& OutValue, MString* OutError)
{
    OutValue.Type = EJsonType::Array;
    OutValue.ArrayValue.clear();
    OutValue.ArrayValue.reserve(Value.size());
    for (const TElement& Element : Value)
    {
        MJsonValue ElementValue;
        if (!ExportReflectValueToJson(Element, ElementValue, OutError))
        {
            return false;
        }
        OutValue.ArrayValue.push_back(std::move(ElementValue));
    }
    return true;
}

template<typename TElement>
inline bool ImportReflectValueFromJson(
    const MJsonValue& InValue,
    TVector<TElement>& OutValue,
    const MProperty* Prop,
    void* OwnerObject,
    MString* OutError)
{
    if (!InValue.IsArray())
    {
        if (OutError)
        {
            *OutError = "json_import_expected_array";
        }
        return false;
    }

    OutValue.clear();
    OutValue.reserve(InValue.ArrayValue.size());
    for (const MJsonValue& ElementValue : InValue.ArrayValue)
    {
        TElement Element{};
        if (!ImportReflectValueFromJson(ElementValue, Element, Prop, OwnerObject, OutError))
        {
            return false;
        }
        OutValue.push_back(std::move(Element));
    }
    return true;
}

template<typename TElement>
inline bool ExportReflectValueToBinary(const TVector<TElement>& Value, TByteArray& OutData, MString* OutError)
{
    OutData.clear();
    MObjectAssetBinary::AppendVarUInt(OutData, static_cast<uint64>(Value.size()));

    for (const TElement& Element : Value)
    {
        TByteArray ElementPayload;
        if (!ExportReflectValueToBinary(Element, ElementPayload, OutError))
        {
            return false;
        }

        MObjectAssetBinary::AppendVarUInt(OutData, static_cast<uint64>(ElementPayload.size()));
        OutData.insert(OutData.end(), ElementPayload.begin(), ElementPayload.end());
    }

    return true;
}

template<typename TElement>
inline bool ImportReflectValueFromBinary(
    const TByteArray& InData,
    TVector<TElement>& OutValue,
    const MProperty* Prop,
    void* OwnerObject,
    MString* OutError)
{
    size_t Offset = 0;
    uint64 Count = 0;
    if (!MObjectAssetBinary::ReadVarUInt(InData, Offset, Count))
    {
        if (OutError)
        {
            *OutError = "binary_import_expected_array";
        }
        return false;
    }

    OutValue.clear();
    OutValue.reserve(static_cast<size_t>(Count));
    for (uint64 Index = 0; Index < Count; ++Index)
    {
        uint64 ElementSize = 0;
        if (!MObjectAssetBinary::ReadVarUInt(InData, Offset, ElementSize) ||
            ElementSize > static_cast<uint64>(InData.size() - Offset))
        {
            if (OutError)
            {
                *OutError = "binary_import_truncated_array_element";
            }
            return false;
        }

        TByteArray ElementPayload(
            InData.begin() + static_cast<std::ptrdiff_t>(Offset),
            InData.begin() + static_cast<std::ptrdiff_t>(Offset + static_cast<size_t>(ElementSize)));
        Offset += static_cast<size_t>(ElementSize);

        TElement Element{};
        if (!ImportReflectValueFromBinary(ElementPayload, Element, Prop, OwnerObject, OutError))
        {
            return false;
        }
        OutValue.push_back(std::move(Element));
    }

    return Detail::EnsureFullyConsumed(InData, Offset, OutError);
}

// TMap<K, V> 容器专用序列化
template<typename K, typename V, typename Compare>
struct TPropertySnapshotOps<TMap<K, V, Compare>>
{
    static void Apply(const MProperty* Prop, void* Object, MReflectArchive& Ar)
    {
        if (!Prop || !Object)
        {
            return;
        }

        auto* MapPtr = Prop->GetValuePtr<TMap<K, V, Compare>>(Object);
        if (!MapPtr)
        {
            return;
        }
        uint32 Count = 0;
        if (Ar.bWriting)
        {
            Count = static_cast<uint32>(MapPtr->size());
        }

        Ar << Count;

        if (Ar.bReading)
        {
            MapPtr->clear();
            for (uint32 Index = 0; Index < Count; ++Index)
            {
                K Key{};
                V Value{};
                Ar << Key;
                Ar << Value;
                MapPtr->emplace(std::move(Key), std::move(Value));
            }
        }
        else
        {
            for (auto& Pair : *MapPtr)
            {
                K KeyCopy = Pair.first;
                V& Value = Pair.second;
                Ar << KeyCopy;
                Ar << Value;
            }
        }
    }
};

template<typename K, typename V, typename Compare>
struct TPropertyStringExporter<TMap<K, V, Compare>>
{
    static MString Export(const MProperty* Prop, const void* Object)
    {
        if (!Prop || !Object)
        {
            return "<null-map>";
        }

        const auto* MapPtr = Prop->GetValuePtr<TMap<K, V, Compare>>(Object);
        if (!MapPtr)
        {
            return "<null-map>";
        }

        MString Result = "{";
        bool bFirst = true;
        for (const auto& Pair : *MapPtr)
        {
            if (!bFirst)
            {
                Result += ", ";
            }
            Result += ReflectValueToString(Pair.first);
            Result += ": ";
            Result += ReflectValueToString(Pair.second);
            bFirst = false;
        }
        Result += "}";
        return Result;
    }
};

// TSet<T> 容器专用序列化
template<typename T, typename Compare>
struct TPropertySnapshotOps<TSet<T, Compare>>
{
    static void Apply(const MProperty* Prop, void* Object, MReflectArchive& Ar)
    {
        if (!Prop || !Object)
        {
            return;
        }

        auto* SetPtr = Prop->GetValuePtr<TSet<T, Compare>>(Object);
        if (!SetPtr)
        {
            return;
        }
        uint32 Count = 0;
        if (Ar.bWriting)
        {
            Count = static_cast<uint32>(SetPtr->size());
        }

        Ar << Count;

        if (Ar.bReading)
        {
            SetPtr->clear();
            for (uint32 Index = 0; Index < Count; ++Index)
            {
                T Value{};
                Ar << Value;
                SetPtr->insert(std::move(Value));
            }
        }
        else
        {
            for (const T& Value : *SetPtr)
            {
                T Copy = Value;
                Ar << Copy;
            }
        }
    }
};

template<typename T, typename Compare>
struct TPropertyStringExporter<TSet<T, Compare>>
{
    static MString Export(const MProperty* Prop, const void* Object)
    {
        if (!Prop || !Object)
        {
            return "<null-set>";
        }

        const auto* SetPtr = Prop->GetValuePtr<TSet<T, Compare>>(Object);
        if (!SetPtr)
        {
            return "<null-set>";
        }

        MString Result = "{";
        bool bFirst = true;
        for (const T& Value : *SetPtr)
        {
            if (!bFirst)
            {
                Result += ", ";
            }
            Result += ReflectValueToString(Value);
            bFirst = false;
        }
        Result += "}";
        return Result;
    }
};

// 向反射系统注册 TVector 容器属性（元素类型必须已被 MReflectArchive 支持）
template<typename TElement>
class MVectorProperty : public MProperty
{
public:
    MVectorProperty(const MString& InName, size_t InOffset, EPropertyFlags InFlags)
        : MProperty(InName,
                    EPropertyType::None,
                    InOffset,
                    sizeof(TVector<TElement>),
                    std::type_index(typeid(TVector<TElement>)))
    {
        Flags = InFlags;
    }

    virtual void WriteValue(void* Object, MReflectArchive& Ar) const override
    {
        if (!Object)
        {
            return;
        }

        auto* Vec = reinterpret_cast<TVector<TElement>*>(reinterpret_cast<uint8*>(Object) + Offset);
        uint32 Count = 0;
        if (Ar.bWriting)
        {
            Count = static_cast<uint32>(Vec->size());
        }

        Ar << Count;

        if (Ar.bReading)
        {
            Vec->resize(Count);
        }

        for (uint32 Index = 0; Index < Count; ++Index)
        {
            TElement& Element = (*Vec)[Index];
            Ar << Element;
        }
    }

    virtual MString ExportValueToString(const void* Object) const override
    {
        if (!Object)
        {
            return "<null-array>";
        }

        const auto* Vec = reinterpret_cast<const TVector<TElement>*>(reinterpret_cast<const uint8*>(Object) + Offset);
        if (!Vec)
        {
            return "<null-array>";
        }

        MString Result = "[";
        for (size_t Index = 0; Index < Vec->size(); ++Index)
        {
            if (Index > 0)
            {
                Result += ", ";
            }
            Result += ReflectValueToString((*Vec)[Index]);
        }
        Result += "]";
        return Result;
    }
};

template<typename TObject, typename TElement, TVector<TElement> TObject::* MemberPtr>
class TMemberVectorProperty : public MProperty
{
public:
    TMemberVectorProperty(const MString& InName, EPropertyFlags InFlags)
        : MProperty(
            InName,
            EPropertyType::None,
            0,
            sizeof(TVector<TElement>),
            std::type_index(typeid(TVector<TElement>)),
            &TMemberVectorProperty::GetMutableValue,
            &TMemberVectorProperty::GetConstValue)
    {
        Flags = InFlags;
    }

    virtual void WriteValue(void* Object, MReflectArchive& Ar) const override
    {
        auto* Vec = GetValuePtr<TVector<TElement>>(Object);
        if (!Vec)
        {
            return;
        }

        uint32 Count = 0;
        if (Ar.bWriting)
        {
            Count = static_cast<uint32>(Vec->size());
        }

        Ar << Count;

        if (Ar.bReading)
        {
            Vec->resize(Count);
        }

        for (uint32 Index = 0; Index < Count; ++Index)
        {
            TElement& Element = (*Vec)[Index];
            Ar << Element;
        }
    }

    virtual MString ExportValueToString(const void* Object) const override
    {
        const auto* Vec = GetValuePtr<TVector<TElement>>(Object);
        if (!Vec)
        {
            return "<null-array>";
        }

        MString Result = "[";
        for (size_t Index = 0; Index < Vec->size(); ++Index)
        {
            if (Index > 0)
            {
                Result += ", ";
            }
            Result += ReflectValueToString((*Vec)[Index]);
        }
        Result += "]";
        return Result;
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

#define REGISTER_TVECTOR_PROPERTY(ElementCppType, PropName, PropFlags) \
    do { \
        auto* Prop = new TMemberVectorProperty<ThisClass, ElementCppType, &ThisClass::PropName>(#PropName, PropFlags); \
        InClass->RegisterProperty(Prop); \
    } while(0)
