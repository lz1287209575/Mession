#include "Common/Runtime/Asset/MObjectAssetJson.h"

#include "Common/Runtime/Object/Object.h"
#include "Common/Runtime/Reflect/Reflection.h"

namespace MObjectAssetJson
{
namespace
{
void GatherClassProperties(const MClass* ClassMeta, bool bAssetOnly, TVector<const MProperty*>& OutProperties)
{
    if (!ClassMeta)
    {
        return;
    }

    GatherClassProperties(ClassMeta->GetParentClass(), bAssetOnly, OutProperties);
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

bool ExportPropertySetToJsonObject(
    const MClass* ClassMeta,
    const void* ObjectData,
    bool bAssetOnly,
    MJsonValue& OutValue,
    MString* OutError)
{
    if (!ClassMeta || !ObjectData)
    {
        if (OutError)
        {
            *OutError = "asset_json_export_invalid_target";
        }
        return false;
    }

    OutValue = MJsonValue{};
    OutValue.Type = EJsonType::Object;

    TVector<const MProperty*> Properties;
    GatherClassProperties(ClassMeta, bAssetOnly, Properties);
    for (const MProperty* Prop : Properties)
    {
        MJsonValue PropertyValue;
        MString PropertyError;
        if (!Prop->ExportJsonValue(ObjectData, PropertyValue, &PropertyError))
        {
            if (OutError)
            {
                *OutError = "asset_json_export_property_failed:" + Prop->Name + ":" + PropertyError;
            }
            return false;
        }
        OutValue.ObjectValue[Prop->Name] = std::move(PropertyValue);
    }

    return true;
}

bool ImportPropertySetFromJsonObject(
    const MClass* ClassMeta,
    void* ObjectData,
    const MJsonValue& InValue,
    bool bAssetOnly,
    MString* OutError)
{
    if (!ClassMeta || !ObjectData)
    {
        if (OutError)
        {
            *OutError = "asset_json_import_invalid_target";
        }
        return false;
    }
    if (!InValue.IsObject())
    {
        if (OutError)
        {
            *OutError = "asset_json_import_expected_object";
        }
        return false;
    }

    for (const auto& [Key, Value] : InValue.ObjectValue)
    {
        if (!Key.empty() && Key[0] == '$')
        {
            continue;
        }

        MProperty* Prop = ClassMeta->FindProperty(Key);
        if (!Prop)
        {
            if (OutError)
            {
                *OutError = "asset_json_unknown_property:" + Key;
            }
            return false;
        }
        if (bAssetOnly && !Prop->HasAnyDomains(EPropertyDomainFlags::Asset))
        {
            if (OutError)
            {
                *OutError = "asset_json_non_asset_property:" + Key;
            }
            return false;
        }

        MString PropertyError;
        if (!Prop->ImportJsonValue(ObjectData, Value, &PropertyError))
        {
            if (OutError)
            {
                *OutError = "asset_json_import_property_failed:" + Key + ":" + PropertyError;
            }
            return false;
        }
    }

    return true;
}

void AppendIndent(MString& Out, int Depth)
{
    Out.append(static_cast<size_t>(Depth) * 2u, ' ');
}

void StringifyJsonValue(const MJsonValue& Value, MString& Out, int Depth)
{
    switch (Value.Type)
    {
    case EJsonType::Null:
        Out += "null";
        return;
    case EJsonType::Boolean:
        Out += Value.BoolValue ? "true" : "false";
        return;
    case EJsonType::Number:
        Out += MStringUtil::ToString(Value.NumberValue);
        return;
    case EJsonType::String:
    {
        MJsonWriter Writer;
        Writer.Value(Value.StringValue);
        Out += Writer.ToString();
        return;
    }
    case EJsonType::Array:
    {
        Out += "[";
        if (!Value.ArrayValue.empty())
        {
            Out += "\n";
            for (size_t Index = 0; Index < Value.ArrayValue.size(); ++Index)
            {
                AppendIndent(Out, Depth + 1);
                StringifyJsonValue(Value.ArrayValue[Index], Out, Depth + 1);
                if (Index + 1 < Value.ArrayValue.size())
                {
                    Out += ",";
                }
                Out += "\n";
            }
            AppendIndent(Out, Depth);
        }
        Out += "]";
        return;
    }
    case EJsonType::Object:
    {
        Out += "{";
        if (!Value.ObjectValue.empty())
        {
            Out += "\n";
            size_t Index = 0;
            for (const auto& [Key, ObjectValue] : Value.ObjectValue)
            {
                AppendIndent(Out, Depth + 1);
                MJsonWriter Writer;
                Writer.Value(Key);
                Out += Writer.ToString();
                Out += ": ";
                StringifyJsonValue(ObjectValue, Out, Depth + 1);
                if (Index + 1 < Value.ObjectValue.size())
                {
                    Out += ",";
                }
                Out += "\n";
                ++Index;
            }
            AppendIndent(Out, Depth);
        }
        Out += "}";
        return;
    }
    }
}

MString JsonValueToString(const MJsonValue& Value)
{
    MString Result;
    StringifyJsonValue(Value, Result, 0);
    return Result;
}

MClass* ResolveClassForImport(const MJsonValue& InValue, MString* OutError)
{
    if (!InValue.IsObject())
    {
        if (OutError)
        {
            *OutError = "asset_json_root_not_object";
        }
        return nullptr;
    }

    const auto ClassIt = InValue.ObjectValue.find("$class");
    if (ClassIt == InValue.ObjectValue.end() || !ClassIt->second.IsString())
    {
        if (OutError)
        {
            *OutError = "asset_json_missing_class";
        }
        return nullptr;
    }

    MClass* ClassMeta = MObject::FindClass(ClassIt->second.StringValue);
    if (!ClassMeta)
    {
        if (OutError)
        {
            *OutError = "asset_json_unknown_class:" + ClassIt->second.StringValue;
        }
        return nullptr;
    }
    return ClassMeta;
}
}

bool ExportStructToJsonValue(const MClass* StructMeta, const void* StructData, MJsonValue& OutValue, MString* OutError)
{
    return ExportPropertySetToJsonObject(StructMeta, StructData, false, OutValue, OutError);
}

bool ImportStructFromJsonValue(const MClass* StructMeta, void* StructData, const MJsonValue& InValue, MString* OutError)
{
    return ImportPropertySetFromJsonObject(StructMeta, StructData, InValue, false, OutError);
}

bool ExportAssetObjectToJsonValue(const MObject* Object, MJsonValue& OutValue, MString* OutError)
{
    if (!Object)
    {
        if (OutError)
        {
            *OutError = "asset_json_export_null_object";
        }
        return false;
    }

    MClass* ClassMeta = Object->GetClass();
    if (!ClassMeta)
    {
        if (OutError)
        {
            *OutError = "asset_json_export_missing_class";
        }
        return false;
    }

    if (!ExportPropertySetToJsonObject(ClassMeta, Object, true, OutValue, OutError))
    {
        return false;
    }

    OutValue.ObjectValue["$class"] = MJsonValue{};
    OutValue.ObjectValue["$class"].Type = EJsonType::String;
    OutValue.ObjectValue["$class"].StringValue = ClassMeta->GetName();

    if (!Object->GetName().empty())
    {
        OutValue.ObjectValue["$name"] = MJsonValue{};
        OutValue.ObjectValue["$name"].Type = EJsonType::String;
        OutValue.ObjectValue["$name"].StringValue = Object->GetName();
    }

    return true;
}

bool ExportAssetObjectToJson(const MObject* Object, MString& OutJson, MString* OutError)
{
    MJsonValue RootValue;
    if (!ExportAssetObjectToJsonValue(Object, RootValue, OutError))
    {
        return false;
    }

    OutJson = JsonValueToString(RootValue);
    return true;
}

bool ImportAssetObjectFieldsFromJsonValue(MObject* Object, const MJsonValue& InValue, MString* OutError)
{
    if (!Object)
    {
        if (OutError)
        {
            *OutError = "asset_json_import_null_object";
        }
        return false;
    }

    MClass* ClassMeta = Object->GetClass();
    if (!ClassMeta)
    {
        if (OutError)
        {
            *OutError = "asset_json_import_missing_class";
        }
        return false;
    }

    return ImportPropertySetFromJsonObject(ClassMeta, Object, InValue, true, OutError);
}

MObject* ImportAssetObjectFromJsonValue(const MJsonValue& InValue, MObject* Outer, MString* OutError)
{
    MClass* ClassMeta = ResolveClassForImport(InValue, OutError);
    if (!ClassMeta)
    {
        return nullptr;
    }

    MObject* Object = static_cast<MObject*>(ClassMeta->CreateInstance());
    if (!Object)
    {
        if (OutError)
        {
            *OutError = "asset_json_create_instance_failed:" + ClassMeta->GetName();
        }
        return nullptr;
    }

    const auto NameIt = InValue.ObjectValue.find("$name");
    if (NameIt != InValue.ObjectValue.end() && NameIt->second.IsString())
    {
        Object->SetName(NameIt->second.StringValue);
    }

    if (Outer)
    {
        Object->SetOuter(Outer);
    }
    else
    {
        Object->AddToRoot();
    }

    MString ImportError;
    if (!ImportAssetObjectFieldsFromJsonValue(Object, InValue, &ImportError))
    {
        delete Object;
        if (OutError)
        {
            *OutError = ImportError;
        }
        return nullptr;
    }

    return Object;
}

MObject* ImportAssetObjectFromJson(const MString& InJson, MObject* Outer, MString* OutError)
{
    MJsonValue RootValue;
    MString ParseError;
    if (!MJsonReader::Parse(InJson, RootValue, ParseError))
    {
        if (OutError)
        {
            *OutError = ParseError;
        }
        return nullptr;
    }

    return ImportAssetObjectFromJsonValue(RootValue, Outer, OutError);
}
}
