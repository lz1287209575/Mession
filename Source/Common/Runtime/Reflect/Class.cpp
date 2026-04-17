#include "Common/Runtime/Reflect/Reflection.h"

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

    auto* ReflectObj = static_cast<MObject*>(Obj);
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

    auto* ReflectObj = static_cast<MObject*>(Object);
    if (ReflectObj)
    {
        ReflectObj->SetClass(const_cast<MClass*>(this));
    }
}

MProperty* MClass::FindProperty(const MString& InName) const
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

MProperty* MClass::FindPropertyByAssetFieldId(uint32 InAssetFieldId) const
{
    for (MProperty* Prop : Properties)
    {
        if (Prop && Prop->AssetFieldId == InAssetFieldId)
        {
            return Prop;
        }
    }

    if (ParentClass)
    {
        return ParentClass->FindPropertyByAssetFieldId(InAssetFieldId);
    }

    return nullptr;
}

MFunction* MClass::FindFunction(const MString& InName) const
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

void MClass::WriteSnapshotByDomain(void* Object, MReflectArchive& Ar, uint64 InDomainMask) const
{
    if (!Object || InDomainMask == 0)
    {
        return;
    }

    for (MProperty* Prop : Properties)
    {
        if (!Prop || (Prop->DomainFlags & InDomainMask) == 0)
        {
            continue;
        }
        Prop->WriteValue(Object, Ar);
    }
}

void MClass::ReadSnapshot(void* Object, const TByteArray& Data) const
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

void MClass::ReadSnapshotByDomain(void* Object, const TByteArray& Data, uint64 InDomainMask) const
{
    if (!Object || InDomainMask == 0)
    {
        return;
    }

    MReflectArchive Ar(Data);

    for (MProperty* Prop : Properties)
    {
        if (!Prop || (Prop->DomainFlags & InDomainMask) == 0)
        {
            continue;
        }
        Prop->WriteValue(Object, Ar);
    }
}

MString MClass::ExportObjectToString(const void* Object) const
{
    if (!Object)
    {
        return ClassName + "{<null>}";
    }

    MString Result = ClassName + "{";
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
