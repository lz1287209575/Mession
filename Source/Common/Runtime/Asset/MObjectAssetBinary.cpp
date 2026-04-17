#include "Common/Runtime/Asset/MObjectAssetBinary.h"

#include "Common/Runtime/Asset/MObjectAssetJson.h"
#include "Common/Runtime/Object/Object.h"

namespace MObjectAssetBinary
{
namespace
{
constexpr uint8 GFileMagic[4] = {'M', 'O', 'B', 'J'};

void AppendUInt16LE(TByteArray& OutBytes, uint16 Value)
{
    AppendFixedLE(OutBytes, Value);
}

void AppendUInt32LE(TByteArray& OutBytes, uint32 Value)
{
    AppendFixedLE(OutBytes, Value);
}

bool ReadUInt16LE(const TByteArray& Bytes, size_t& Offset, uint16& OutValue)
{
    return ReadFixedLE(Bytes, Offset, OutValue);
}

bool ReadUInt32LE(const TByteArray& Bytes, size_t& Offset, uint32& OutValue)
{
    return ReadFixedLE(Bytes, Offset, OutValue);
}

void BuildHeaderBytes(const SFileHeader& Header, TByteArray& OutBytes)
{
    OutBytes.insert(OutBytes.end(), std::begin(GFileMagic), std::end(GFileMagic));
    AppendUInt16LE(OutBytes, Header.Version);
    AppendUInt16LE(OutBytes, Header.Flags);
    AppendUInt32LE(OutBytes, Header.RootAssetTypeId);
    AppendUInt32LE(OutBytes, static_cast<uint32>(Header.PayloadEncoding));
    AppendUInt32LE(OutBytes, Header.PayloadSize);
}

void GatherSerializableProperties(const MClass* ClassMeta, bool bAssetOnly, TVector<const MProperty*>& OutProperties)
{
    if (!ClassMeta)
    {
        return;
    }

    GatherSerializableProperties(ClassMeta->GetParentClass(), bAssetOnly, OutProperties);
    for (const MProperty* Prop : ClassMeta->GetProperties())
    {
        if (!Prop)
        {
            continue;
        }
        if (bAssetOnly && !Prop->HasAnyDomains(EPropertyDomainFlags::Asset))
        {
            continue;
        }
        OutProperties.push_back(Prop);
    }
}

bool EncodeFieldRecords(
    const MClass* ClassMeta,
    const void* ObjectData,
    bool bAssetOnly,
    TByteArray& OutPayload,
    MString* OutError)
{
    if (!ClassMeta || !ObjectData)
    {
        if (OutError)
        {
            *OutError = "asset_binary_encode_invalid_target";
        }
        return false;
    }

    OutPayload.clear();
    TVector<const MProperty*> Properties;
    GatherSerializableProperties(ClassMeta, bAssetOnly, Properties);
    AppendVarUInt(OutPayload, static_cast<uint64>(Properties.size()));

    for (const MProperty* Prop : Properties)
    {
        TByteArray FieldPayload;
        MString FieldError;
        if (!Prop->ExportBinaryValue(ObjectData, FieldPayload, &FieldError))
        {
            if (OutError)
            {
                *OutError = "asset_binary_encode_field_failed:" + Prop->Name + ":" + FieldError;
            }
            return false;
        }

        AppendUInt32LE(OutPayload, Prop->AssetFieldId);
        AppendByte(OutPayload, static_cast<uint8>(Prop->Type));
        AppendVarUInt(OutPayload, static_cast<uint64>(FieldPayload.size()));
        OutPayload.insert(OutPayload.end(), FieldPayload.begin(), FieldPayload.end());
    }

    return true;
}

bool DecodeFieldRecords(
    const MClass* ClassMeta,
    void* ObjectData,
    bool bAssetOnly,
    const TByteArray& InPayload,
    MString* OutError)
{
    if (!ClassMeta || !ObjectData)
    {
        if (OutError)
        {
            *OutError = "asset_binary_decode_invalid_target";
        }
        return false;
    }

    size_t Offset = 0;
    uint64 FieldCount = 0;
    if (!ReadVarUInt(InPayload, Offset, FieldCount))
    {
        if (OutError)
        {
            *OutError = "asset_binary_decode_field_count_failed";
        }
        return false;
    }

    for (uint64 FieldIndex = 0; FieldIndex < FieldCount; ++FieldIndex)
    {
        uint32 AssetFieldId = 0;
        uint8 EncodedType = 0;
        uint64 PayloadSize = 0;
        if (!ReadUInt32LE(InPayload, Offset, AssetFieldId) ||
            !ReadByte(InPayload, Offset, EncodedType) ||
            !ReadVarUInt(InPayload, Offset, PayloadSize) ||
            PayloadSize > static_cast<uint64>(InPayload.size() - Offset))
        {
            if (OutError)
            {
                *OutError = "asset_binary_decode_field_header_failed";
            }
            return false;
        }

        const size_t PayloadOffset = Offset;
        Offset += static_cast<size_t>(PayloadSize);

        MProperty* Prop = ClassMeta->FindPropertyByAssetFieldId(AssetFieldId);
        if (!Prop)
        {
            continue;
        }
        if (bAssetOnly && !Prop->HasAnyDomains(EPropertyDomainFlags::Asset))
        {
            continue;
        }
        if (static_cast<uint8>(Prop->Type) != EncodedType)
        {
            if (OutError)
            {
                *OutError = "asset_binary_field_type_mismatch:" + Prop->Name;
            }
            return false;
        }

        TByteArray FieldPayload(
            InPayload.begin() + static_cast<std::ptrdiff_t>(PayloadOffset),
            InPayload.begin() + static_cast<std::ptrdiff_t>(PayloadOffset + static_cast<size_t>(PayloadSize)));

        MString FieldError;
        if (!Prop->ImportBinaryValue(ObjectData, FieldPayload, &FieldError))
        {
            if (OutError)
            {
                *OutError = "asset_binary_decode_field_failed:" + Prop->Name + ":" + FieldError;
            }
            return false;
        }
    }

    if (Offset != InPayload.size())
    {
        if (OutError)
        {
            *OutError = "asset_binary_decode_trailing_bytes";
        }
        return false;
    }

    return true;
}

bool BuildTaggedPayload(const MObject* RootObject, TByteArray& OutPayload, MString* OutError)
{
    if (!RootObject)
    {
        if (OutError)
        {
            *OutError = "asset_binary_null_root";
        }
        return false;
    }

    MClass* ClassMeta = RootObject->GetClass();
    if (!ClassMeta)
    {
        if (OutError)
        {
            *OutError = "asset_binary_missing_class";
        }
        return false;
    }

    TByteArray FieldPayload;
    if (!EncodeFieldRecords(ClassMeta, RootObject, true, FieldPayload, OutError))
    {
        return false;
    }

    OutPayload.clear();
    AppendVarUInt(OutPayload, 1u);
    AppendVarUInt(OutPayload, 1u);
    AppendVarUInt(OutPayload, 1u);
    AppendVarUInt(OutPayload, 0u);
    AppendString(OutPayload, RootObject->GetName());
    AppendUInt32LE(OutPayload, ClassMeta->GetAssetTypeId());
    OutPayload.insert(OutPayload.end(), FieldPayload.begin(), FieldPayload.end());
    return true;
}

MObject* LoadTaggedPayload(const TByteArray& Payload, MObject* Outer, MString* OutError)
{
    size_t Offset = 0;
    uint64 ObjectCount = 0;
    uint64 RootIndex = 0;
    uint64 ObjectIndex = 0;
    uint64 ParentIndex = 0;
    uint32 AssetTypeId = 0;
    if (!ReadVarUInt(Payload, Offset, ObjectCount) ||
        !ReadVarUInt(Payload, Offset, RootIndex) ||
        !ReadVarUInt(Payload, Offset, ObjectIndex) ||
        !ReadVarUInt(Payload, Offset, ParentIndex))
    {
        if (OutError)
        {
            *OutError = "asset_binary_decode_object_header_failed";
        }
        return nullptr;
    }

    if (ObjectCount != 1u || RootIndex != 1u || ObjectIndex != 1u || ParentIndex != 0u)
    {
        if (OutError)
        {
            *OutError = "asset_binary_object_table_unsupported";
        }
        return nullptr;
    }

    MString ObjectName;
    if (!ReadString(Payload, Offset, ObjectName) ||
        !ReadUInt32LE(Payload, Offset, AssetTypeId))
    {
        if (OutError)
        {
            *OutError = "asset_binary_decode_object_meta_failed";
        }
        return nullptr;
    }

    MClass* ClassMeta = MObject::FindClassByAssetTypeId(AssetTypeId);
    if (!ClassMeta)
    {
        if (OutError)
        {
            *OutError = "asset_binary_unknown_asset_type_id:" + std::to_string(AssetTypeId);
        }
        return nullptr;
    }

    MObject* RootObject = static_cast<MObject*>(ClassMeta->CreateInstance());
    if (!RootObject)
    {
        if (OutError)
        {
            *OutError = "asset_binary_create_instance_failed:" + ClassMeta->GetName();
        }
        return nullptr;
    }

    RootObject->SetName(ObjectName);
    if (Outer)
    {
        RootObject->SetOuter(Outer);
    }
    else
    {
        RootObject->AddToRoot();
    }

    TByteArray FieldPayload(
        Payload.begin() + static_cast<std::ptrdiff_t>(Offset),
        Payload.end());
    MString DecodeError;
    if (!DecodeFieldRecords(ClassMeta, RootObject, true, FieldPayload, &DecodeError))
    {
        delete RootObject;
        if (OutError)
        {
            *OutError = DecodeError;
        }
        return nullptr;
    }

    return RootObject;
}
}

bool EncodeStructFields(const MClass* StructMeta, const void* StructData, TByteArray& OutData, MString* OutError)
{
    return EncodeFieldRecords(StructMeta, StructData, false, OutData, OutError);
}

bool DecodeStructFields(const MClass* StructMeta, void* StructData, const TByteArray& InData, MString* OutError)
{
    return DecodeFieldRecords(StructMeta, StructData, false, InData, OutError);
}

bool BuildFromObject(const MObject* RootObject, TByteArray& OutBytes, MString* OutError)
{
    if (!RootObject)
    {
        if (OutError)
        {
            *OutError = "asset_binary_null_root";
        }
        return false;
    }

    MClass* ClassMeta = RootObject->GetClass();
    if (!ClassMeta)
    {
        if (OutError)
        {
            *OutError = "asset_binary_missing_class";
        }
        return false;
    }

    TByteArray Payload;
    if (!BuildTaggedPayload(RootObject, Payload, OutError))
    {
        return false;
    }

    SFileHeader Header;
    Header.RootAssetTypeId = ClassMeta->GetAssetTypeId();
    Header.PayloadEncoding = EPayloadEncoding::TaggedFields;
    Header.PayloadSize = static_cast<uint32>(Payload.size());

    OutBytes.clear();
    OutBytes.reserve(sizeof(GFileMagic) + 2 + 2 + 4 + 4 + 4 + Payload.size());
    BuildHeaderBytes(Header, OutBytes);
    OutBytes.insert(OutBytes.end(), Payload.begin(), Payload.end());
    return true;
}

bool BuildFromJson(const MString& JsonText, TByteArray& OutBytes, MString* OutError)
{
    MObject* RootObject = MObjectAssetJson::ImportAssetObjectFromJson(JsonText, nullptr, OutError);
    if (!RootObject)
    {
        return false;
    }

    const bool bSuccess = BuildFromObject(RootObject, OutBytes, OutError);
    delete RootObject;
    return bSuccess;
}

bool ReadHeader(const TByteArray& Bytes, SFileHeader& OutHeader, size_t& OutPayloadOffset, MString* OutError)
{
    OutHeader = SFileHeader{};
    OutPayloadOffset = 0;

    if (Bytes.size() < sizeof(GFileMagic) + 2 + 2 + 4 + 4 + 4)
    {
        if (OutError)
        {
            *OutError = "asset_binary_truncated_header";
        }
        return false;
    }

    if (!std::equal(std::begin(GFileMagic), std::end(GFileMagic), Bytes.begin()))
    {
        if (OutError)
        {
            *OutError = "asset_binary_bad_magic";
        }
        return false;
    }

    size_t Offset = sizeof(GFileMagic);
    uint32 PayloadEncoding = 0;
    if (!ReadUInt16LE(Bytes, Offset, OutHeader.Version) ||
        !ReadUInt16LE(Bytes, Offset, OutHeader.Flags) ||
        !ReadUInt32LE(Bytes, Offset, OutHeader.RootAssetTypeId) ||
        !ReadUInt32LE(Bytes, Offset, PayloadEncoding) ||
        !ReadUInt32LE(Bytes, Offset, OutHeader.PayloadSize))
    {
        if (OutError)
        {
            *OutError = "asset_binary_read_header_failed";
        }
        return false;
    }

    if (OutHeader.Version != FileVersion)
    {
        if (OutError)
        {
            *OutError = "asset_binary_unsupported_version:" + std::to_string(OutHeader.Version);
        }
        return false;
    }

    OutHeader.PayloadEncoding = static_cast<EPayloadEncoding>(PayloadEncoding);
    if (OutHeader.PayloadEncoding != EPayloadEncoding::JsonBridge &&
        OutHeader.PayloadEncoding != EPayloadEncoding::TaggedFields)
    {
        if (OutError)
        {
            *OutError = "asset_binary_unsupported_payload_encoding:" + std::to_string(PayloadEncoding);
        }
        return false;
    }

    if (Offset + OutHeader.PayloadSize > Bytes.size())
    {
        if (OutError)
        {
            *OutError = "asset_binary_truncated_payload";
        }
        return false;
    }

    OutPayloadOffset = Offset;
    return true;
}

bool ExtractPayloadJson(const TByteArray& Bytes, MString& OutJson, SFileHeader* OutHeader, MString* OutError)
{
    SFileHeader Header;
    size_t PayloadOffset = 0;
    if (!ReadHeader(Bytes, Header, PayloadOffset, OutError))
    {
        return false;
    }

    if (Header.PayloadEncoding != EPayloadEncoding::JsonBridge)
    {
        if (OutError)
        {
            *OutError = "asset_binary_payload_not_json_bridge";
        }
        return false;
    }

    OutJson.assign(
        reinterpret_cast<const char*>(Bytes.data() + PayloadOffset),
        static_cast<size_t>(Header.PayloadSize));

    if (OutHeader)
    {
        *OutHeader = Header;
    }
    return true;
}

MObject* LoadFromBytes(const TByteArray& Bytes, MObject* Outer, MString* OutError)
{
    SFileHeader Header;
    size_t PayloadOffset = 0;
    if (!ReadHeader(Bytes, Header, PayloadOffset, OutError))
    {
        return nullptr;
    }

    if (Header.PayloadEncoding == EPayloadEncoding::JsonBridge)
    {
        MString JsonPayload(
            reinterpret_cast<const char*>(Bytes.data() + PayloadOffset),
            static_cast<size_t>(Header.PayloadSize));

        MObject* RootObject = MObjectAssetJson::ImportAssetObjectFromJson(JsonPayload, Outer, OutError);
        if (!RootObject)
        {
            return nullptr;
        }

        MClass* RootClass = RootObject->GetClass();
        if (!RootClass || RootClass->GetAssetTypeId() != Header.RootAssetTypeId)
        {
            if (OutError)
            {
                *OutError = "asset_binary_root_type_mismatch";
            }
            delete RootObject;
            return nullptr;
        }

        return RootObject;
    }

    TByteArray Payload(
        Bytes.begin() + static_cast<std::ptrdiff_t>(PayloadOffset),
        Bytes.begin() + static_cast<std::ptrdiff_t>(PayloadOffset + static_cast<size_t>(Header.PayloadSize)));
    MObject* RootObject = LoadTaggedPayload(Payload, Outer, OutError);
    if (!RootObject)
    {
        return nullptr;
    }

    MClass* RootClass = RootObject->GetClass();
    if (!RootClass || RootClass->GetAssetTypeId() != Header.RootAssetTypeId)
    {
        if (OutError)
        {
            *OutError = "asset_binary_root_type_mismatch";
        }
        delete RootObject;
        return nullptr;
    }

    return RootObject;
}
}
