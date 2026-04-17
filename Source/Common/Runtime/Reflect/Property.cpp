#include "Common/Runtime/Reflect/Reflection.h"
#include "Common/Runtime/HexUtils.h"
#include "Common/Runtime/Json.h"
#include "Common/Runtime/Log/Logger.h"

namespace
{
MString FormatVector(const SVector& Value)
{
    return "{X=" + MStringUtil::ToString(Value.X) +
           ", Y=" + MStringUtil::ToString(Value.Y) +
           ", Z=" + MStringUtil::ToString(Value.Z) + "}";
}

MString FormatRotator(const SRotator& Value)
{
    return "{Pitch=" + MStringUtil::ToString(Value.Pitch) +
           ", Yaw=" + MStringUtil::ToString(Value.Yaw) +
           ", Roll=" + MStringUtil::ToString(Value.Roll) + "}";
}
}

void MProperty::WriteValue(void* Object, MReflectArchive& Ar) const
{
    if (!Object || Size == 0)
    {
        return;
    }

    uint8* FieldPtr = static_cast<uint8*>(GetValueVoidPtr(Object));
    if (!FieldPtr)
    {
        return;
    }

    switch (Type)
    {
    case EPropertyType::Int8:
    {
        int8& Value = *reinterpret_cast<int8*>(FieldPtr);
        Ar << Value;
        break;
    }
    case EPropertyType::Int16:
    {
        int16& Value = *reinterpret_cast<int16*>(FieldPtr);
        Ar << Value;
        break;
    }
    case EPropertyType::Int32:
    {
        int32& Value = *reinterpret_cast<int32*>(FieldPtr);
        Ar << Value;
        break;
    }
    case EPropertyType::Int64:
    {
        int64& Value = *reinterpret_cast<int64*>(FieldPtr);
        Ar << Value;
        break;
    }
    case EPropertyType::UInt8:
    {
        uint8& Value = *reinterpret_cast<uint8*>(FieldPtr);
        Ar << Value;
        break;
    }
    case EPropertyType::UInt16:
    {
        uint16& Value = *reinterpret_cast<uint16*>(FieldPtr);
        Ar << Value;
        break;
    }
    case EPropertyType::UInt32:
    {
        uint32& Value = *reinterpret_cast<uint32*>(FieldPtr);
        Ar << Value;
        break;
    }
    case EPropertyType::UInt64:
    {
        uint64& Value = *reinterpret_cast<uint64*>(FieldPtr);
        Ar << Value;
        break;
    }
    case EPropertyType::Float:
    {
        float& Value = *reinterpret_cast<float*>(FieldPtr);
        Ar << Value;
        break;
    }
    case EPropertyType::Double:
    {
        double& Value = *reinterpret_cast<double*>(FieldPtr);
        Ar << Value;
        break;
    }
    case EPropertyType::Bool:
    {
        bool& Value = *reinterpret_cast<bool*>(FieldPtr);
        Ar << Value;
        break;
    }
    case EPropertyType::String:
    {
        MString& Value = *reinterpret_cast<MString*>(FieldPtr);
        Ar << Value;
        break;
    }
    case EPropertyType::Vector:
    {
        SVector& Value = *reinterpret_cast<SVector*>(FieldPtr);
        Ar << Value;
        break;
    }
    case EPropertyType::Rotator:
    {
        SRotator& Value = *reinterpret_cast<SRotator*>(FieldPtr);
        Ar << Value;
        break;
    }
    case EPropertyType::Struct:
    {
        Ar.WriteBytes(FieldPtr, Size);
        break;
    }
    case EPropertyType::Enum:
    {
        Ar.WriteBytes(FieldPtr, Size);
        break;
    }
    default:
        LOG_WARN("Reflection snapshot write: unsupported property type %d for '%s'",
                 (int)Type, Name.c_str());
        break;
    }
}

MString MProperty::ExportValueToString(const void* Object) const
{
    if (!Object)
    {
        return "<null-object>";
    }

    const uint8* FieldPtr = static_cast<const uint8*>(GetValueVoidPtr(Object));
    if (!FieldPtr)
    {
        return "<null-field>";
    }

    switch (Type)
    {
    case EPropertyType::Int8:
        return MStringUtil::ToString(static_cast<int32>(*reinterpret_cast<const int8*>(FieldPtr)));
    case EPropertyType::Int16:
        return MStringUtil::ToString(static_cast<int32>(*reinterpret_cast<const int16*>(FieldPtr)));
    case EPropertyType::Int32:
        return MStringUtil::ToString(*reinterpret_cast<const int32*>(FieldPtr));
    case EPropertyType::Int64:
        return MStringUtil::ToString(*reinterpret_cast<const int64*>(FieldPtr));
    case EPropertyType::UInt8:
        return MStringUtil::ToString(static_cast<uint32>(*reinterpret_cast<const uint8*>(FieldPtr)));
    case EPropertyType::UInt16:
        return MStringUtil::ToString(static_cast<uint32>(*reinterpret_cast<const uint16*>(FieldPtr)));
    case EPropertyType::UInt32:
        return MStringUtil::ToString(*reinterpret_cast<const uint32*>(FieldPtr));
    case EPropertyType::UInt64:
        return MStringUtil::ToString(*reinterpret_cast<const uint64*>(FieldPtr));
    case EPropertyType::Float:
        return MStringUtil::ToString(*reinterpret_cast<const float*>(FieldPtr));
    case EPropertyType::Double:
        return MStringUtil::ToString(*reinterpret_cast<const double*>(FieldPtr));
    case EPropertyType::Bool:
        return *reinterpret_cast<const bool*>(FieldPtr) ? "true" : "false";
    case EPropertyType::String:
        return "\"" + *reinterpret_cast<const MString*>(FieldPtr) + "\"";
    case EPropertyType::Vector:
        return FormatVector(*reinterpret_cast<const SVector*>(FieldPtr));
    case EPropertyType::Rotator:
        return FormatRotator(*reinterpret_cast<const SRotator*>(FieldPtr));
    case EPropertyType::Enum:
    {
        if (const MEnum* EnumMeta = MObject::FindEnum(CppTypeIndex))
        {
            int64 EnumValue = 0;
            switch (Size)
            {
            case 1: EnumValue = static_cast<int64>(*reinterpret_cast<const uint8*>(FieldPtr)); break;
            case 2: EnumValue = static_cast<int64>(*reinterpret_cast<const uint16*>(FieldPtr)); break;
            case 4: EnumValue = static_cast<int64>(*reinterpret_cast<const uint32*>(FieldPtr)); break;
            case 8: EnumValue = static_cast<int64>(*reinterpret_cast<const uint64*>(FieldPtr)); break;
            default: return "<enum-size-unsupported>";
            }

            if (const MEnumValue* ValueMeta = EnumMeta->FindValueByIntegral(EnumValue))
            {
                return EnumMeta->GetName() + "::" + ValueMeta->Name;
            }
            return EnumMeta->GetName() + "::" + MStringUtil::ToString(EnumValue);
        }
        switch (Size)
        {
        case 1: return MStringUtil::ToString(static_cast<uint32>(*reinterpret_cast<const uint8*>(FieldPtr)));
        case 2: return MStringUtil::ToString(static_cast<uint32>(*reinterpret_cast<const uint16*>(FieldPtr)));
        case 4: return MStringUtil::ToString(*reinterpret_cast<const uint32*>(FieldPtr));
        case 8: return MStringUtil::ToString(*reinterpret_cast<const uint64*>(FieldPtr));
        default: return "<enum-size-unsupported>";
        }
    }
    case EPropertyType::Struct:
        if (CppTypeIndex == std::type_index(typeid(SVector)))
        {
            return FormatVector(*reinterpret_cast<const SVector*>(FieldPtr));
        }
        if (CppTypeIndex == std::type_index(typeid(SRotator)))
        {
            return FormatRotator(*reinterpret_cast<const SRotator*>(FieldPtr));
        }
        if (MClass* StructMeta = MObject::FindStruct(CppTypeIndex))
        {
            return StructMeta->ExportObjectToString(FieldPtr);
        }
        return "<struct:" + Name + " hex=" + Hex::BytesToHexString(FieldPtr, Size) + ">";
    case EPropertyType::Array:
        return "<array>";
    case EPropertyType::Object:
        return "<object>";
    case EPropertyType::Class:
        return "<class>";
    default:
        return "<unsupported>";
    }
}

bool MProperty::ExportJsonValue(const void* /*Object*/, MJsonValue& /*OutValue*/, MString* OutError) const
{
    if (OutError)
    {
        *OutError = "json_export_unsupported:" + Name;
    }
    return false;
}

bool MProperty::ImportJsonValue(void* /*Object*/, const MJsonValue& /*InValue*/, MString* OutError) const
{
    if (OutError)
    {
        *OutError = "json_import_unsupported:" + Name;
    }
    return false;
}

bool MProperty::ExportBinaryValue(const void* /*Object*/, TByteArray& /*OutData*/, MString* OutError) const
{
    if (OutError)
    {
        *OutError = "binary_export_unsupported:" + Name;
    }
    return false;
}

bool MProperty::ImportBinaryValue(void* /*Object*/, const TByteArray& /*InData*/, MString* OutError) const
{
    if (OutError)
    {
        *OutError = "binary_import_unsupported:" + Name;
    }
    return false;
}
