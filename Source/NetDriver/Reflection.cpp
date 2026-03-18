#include "NetDriver/Reflection.h"
#include "Common/Logger.h"

// MProperty 序列化实现（默认按基础类型 / 简单结构体处理）

namespace
{
FString BytesToHexString(const uint8* Data, size_t Size)
{
    static const char* HexDigits = "0123456789ABCDEF";
    if (!Data || Size == 0)
    {
        return "";
    }

    FString Result;
    Result.reserve(Size * 2);
    for (size_t Index = 0; Index < Size; ++Index)
    {
        const uint8 Value = Data[Index];
        Result.push_back(HexDigits[(Value >> 4) & 0x0F]);
        Result.push_back(HexDigits[Value & 0x0F]);
    }
    return Result;
}

FString FormatVector(const SVector& Value)
{
    return "{X=" + MString::ToString(Value.X) +
           ", Y=" + MString::ToString(Value.Y) +
           ", Z=" + MString::ToString(Value.Z) + "}";
}

FString FormatRotator(const SRotator& Value)
{
    return "{Pitch=" + MString::ToString(Value.Pitch) +
           ", Yaw=" + MString::ToString(Value.Yaw) +
           ", Roll=" + MString::ToString(Value.Roll) + "}";
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
        FString& Value = *reinterpret_cast<FString*>(FieldPtr);
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
        // 目前对 Struct 做按字节序列化，适合仅包含 POD 字段的简单结构体。
        Ar.WriteBytes(FieldPtr, Size);
        break;
    }
    case EPropertyType::Enum:
    {
        Ar.WriteBytes(FieldPtr, Size);
        break;
    }
    default:
        // 复杂类型（Object / Class / Enum 等）暂未自动支持，需要自定义序列化。
        LOG_WARN("Reflection snapshot write: unsupported property type %d for '%s'",
                 (int)Type, Name.c_str());
        break;
    }
}

FString MProperty::ExportValueToString(const void* Object) const
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
        return MString::ToString(static_cast<int32>(*reinterpret_cast<const int8*>(FieldPtr)));
    case EPropertyType::Int16:
        return MString::ToString(static_cast<int32>(*reinterpret_cast<const int16*>(FieldPtr)));
    case EPropertyType::Int32:
        return MString::ToString(*reinterpret_cast<const int32*>(FieldPtr));
    case EPropertyType::Int64:
        return MString::ToString(*reinterpret_cast<const int64*>(FieldPtr));
    case EPropertyType::UInt8:
        return MString::ToString(static_cast<uint32>(*reinterpret_cast<const uint8*>(FieldPtr)));
    case EPropertyType::UInt16:
        return MString::ToString(static_cast<uint32>(*reinterpret_cast<const uint16*>(FieldPtr)));
    case EPropertyType::UInt32:
        return MString::ToString(*reinterpret_cast<const uint32*>(FieldPtr));
    case EPropertyType::UInt64:
        return MString::ToString(*reinterpret_cast<const uint64*>(FieldPtr));
    case EPropertyType::Float:
        return MString::ToString(*reinterpret_cast<const float*>(FieldPtr));
    case EPropertyType::Double:
        return MString::ToString(*reinterpret_cast<const double*>(FieldPtr));
    case EPropertyType::Bool:
        return *reinterpret_cast<const bool*>(FieldPtr) ? "true" : "false";
    case EPropertyType::String:
        return "\"" + *reinterpret_cast<const FString*>(FieldPtr) + "\"";
    case EPropertyType::Vector:
        return FormatVector(*reinterpret_cast<const SVector*>(FieldPtr));
    case EPropertyType::Rotator:
        return FormatRotator(*reinterpret_cast<const SRotator*>(FieldPtr));
    case EPropertyType::Enum:
    {
        if (const MEnum* EnumMeta = MReflectObject::FindEnum(CppTypeIndex))
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
            return EnumMeta->GetName() + "::" + MString::ToString(EnumValue);
        }
        switch (Size)
        {
        case 1: return MString::ToString(static_cast<uint32>(*reinterpret_cast<const uint8*>(FieldPtr)));
        case 2: return MString::ToString(static_cast<uint32>(*reinterpret_cast<const uint16*>(FieldPtr)));
        case 4: return MString::ToString(*reinterpret_cast<const uint32*>(FieldPtr));
        case 8: return MString::ToString(*reinterpret_cast<const uint64*>(FieldPtr));
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
        if (MClass* StructMeta = MReflectObject::FindStruct(CppTypeIndex))
        {
            return StructMeta->ExportObjectToString(FieldPtr);
        }
        return "<struct:" + Name + " hex=" + BytesToHexString(FieldPtr, Size) + ">";
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

// MClass 实现

MClass::MClass()
{
    ClassId = ++GlobalClassId;
}

MClass::~MClass()
{
    for (MProperty* Prop : Properties)
    {
        delete Prop;
    }
    for (MFunction* Func : Functions)
    {
        delete Func;
    }
}

void* MClass::CreateInstance() const
{
    if (!Constructor)
    {
        return nullptr;
    }

    void* Obj = Constructor(nullptr);

    // 约定：所有可反射类型都继承自 MReflectObject
    auto* ReflectObj = static_cast<MReflectObject*>(Obj);
    if (ReflectObj)
    {
        ReflectObj->SetClass(const_cast<MClass*>(this));
    }

    return Obj;
}

void MClass::Construct(void* Object) const
{
    if (!Constructor || !Object)
    {
        return;
    }

    Constructor(Object);

    auto* ReflectObj = static_cast<MReflectObject*>(Object);
    if (ReflectObj)
    {
        ReflectObj->SetClass(const_cast<MClass*>(this));
    }
}

MProperty* MClass::FindProperty(const FString& InName) const
{
    for (MProperty* Prop : Properties)
    {
        if (Prop && Prop->Name == InName)
        {
            return Prop;
        }
    }

    if (ParentClass)
    {
        return ParentClass->FindProperty(InName);
    }

    return nullptr;
}

MProperty* MClass::FindPropertyById(uint16 InId) const
{
    for (MProperty* Prop : Properties)
    {
        if (Prop && Prop->PropertyId == InId)
        {
            return Prop;
        }
    }

    if (ParentClass)
    {
        return ParentClass->FindPropertyById(InId);
    }

    return nullptr;
}

MFunction* MClass::FindFunction(const FString& InName) const
{
    for (MFunction* Func : Functions)
    {
        if (Func && Func->Name == InName)
        {
            return Func;
        }
    }

    if (ParentClass)
    {
        return ParentClass->FindFunction(InName);
    }

    return nullptr;
}

MFunction* MClass::FindFunctionById(uint16 InId) const
{
    for (MFunction* Func : Functions)
    {
        if (Func && Func->FunctionId == InId)
        {
            return Func;
        }
    }

    if (ParentClass)
    {
        return ParentClass->FindFunctionById(InId);
    }

    return nullptr;
}

void MClass::WriteSnapshot(void* Object, MReflectArchive& Ar) const
{
    if (!Object)
    {
        return;
    }

    for (MProperty* Prop : Properties)
    {
        if (!Prop)
        {
            continue;
        }
        Prop->WriteValue(Object, Ar);
    }
}

void MClass::ReadSnapshot(void* Object, const TArray& Data) const
{
    if (!Object)
    {
        return;
    }

    MReflectArchive Ar(Data);

    for (MProperty* Prop : Properties)
    {
        if (!Prop)
        {
            continue;
        }
        Prop->WriteValue(Object, Ar);
    }
}

FString MClass::ExportObjectToString(const void* Object) const
{
    if (!Object)
    {
        return ClassName + "{<null>}";
    }

    FString Result = ClassName + "{";
    bool bFirst = true;
    if (ParentClass)
    {
        const TVector<MProperty*>& ParentProperties = ParentClass->GetProperties();
        for (const MProperty* Prop : ParentProperties)
        {
            if (!Prop)
            {
                continue;
            }

            if (!bFirst)
            {
                Result += ", ";
            }
            Result += Prop->Name + "=" + Prop->ExportValueToString(Object);
            bFirst = false;
        }
    }

    for (const MProperty* Prop : Properties)
    {
        if (!Prop)
        {
            continue;
        }

        if (!bFirst)
        {
            Result += ", ";
        }
        Result += Prop->Name + "=" + Prop->ExportValueToString(Object);
        bFirst = false;
    }

    Result += "}";
    return Result;
}

FString MReflectObject::ToString() const
{
    MClass* LocalClass = GetClass();
    if (!LocalClass)
    {
        return "MReflectObject{Class=<null>, ObjectId=" + MString::ToString(ObjectId) + "}";
    }

    FString Body = LocalClass->ExportObjectToString(this);
    if (!Name.empty())
    {
        Body += " [Name=\"" + Name + "\"]";
    }
    Body += " [ObjectId=" + MString::ToString(ObjectId) + "]";
    return Body;
}

void MClass::CopyProperties(void* Dest, const void* Src) const
{
    if (!Dest || !Src)
    {
        return;
    }

    for (MProperty* Prop : Properties)
    {
        if (!Prop || Prop->Size == 0)
        {
            continue;
        }

        void* DestPtr = Prop->GetValueVoidPtr(Dest);
        const void* SrcPtr = Prop->GetValueVoidPtr(Src);
        if (!DestPtr || !SrcPtr)
        {
            continue;
        }
        memcpy(DestPtr, SrcPtr, Prop->Size);
    }
}

// MReflectObject 实现

bool MReflectObject::CallFunction(const FString& InName)
{
    return InvokeFunction<void>(InName, nullptr);
}
