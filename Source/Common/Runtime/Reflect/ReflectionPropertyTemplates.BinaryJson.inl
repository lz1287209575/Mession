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
