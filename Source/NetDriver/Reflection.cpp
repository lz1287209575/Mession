#include "NetDriver/Reflection.h"

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

void MClass::Serialize(void* Object, MReflectArchive& Ar) const
{
    (void)Object;
    (void)Ar;
}

void MClass::Deserialize(void* Object, const TArray& Data) const
{
    (void)Object;
    (void)Data;
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

        void* DestPtr = reinterpret_cast<uint8*>(Dest) + Prop->Offset;
        const void* SrcPtr = reinterpret_cast<const uint8*>(Src) + Prop->Offset;
        memcpy(DestPtr, SrcPtr, Prop->Size);
    }
}

// MReflectObject 实现

bool MReflectObject::CallFunction(const FString& InName)
{
    MClass* LocalClass = GetClass();
    if (!LocalClass)
    {
        return false;
    }

    MFunction* Func = LocalClass->FindFunction(InName);
    if (!Func || !Func->NativeFunc)
    {
        return false;
    }

    Func->NativeFunc(this);
    return true;
}

