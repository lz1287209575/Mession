#include "Common/Runtime/Reflect/Reflection.h"

MObject::~MObject()
{
    RemoveFromRoot();
    SetOuter(nullptr);
    GetObjectMap().erase(ObjectId);

    TVector<MObject*> ChildObjects = Children;
    Children.clear();
    for (MObject* Child : ChildObjects)
    {
        delete Child;
    }
}

TMap<uint64, MObject*>& MObject::GetObjectMap()
{
    static TMap<uint64, MObject*> ObjectMap;
    return ObjectMap;
}

TSet<MObject*>& MObject::GetRootSet()
{
    static TSet<MObject*> RootSet;
    return RootSet;
}

void MObject::VisitReferencedObjects(const TFunction<void(MObject*)>& Visitor) const
{
    if (!Visitor)
    {
        return;
    }

    for (MObject* Child : Children)
    {
        if (Child)
        {
            Visitor(Child);
        }
    }
}

void MObject::SetOuter(MObject* InOuter)
{
    if (Outer == InOuter)
    {
        return;
    }

    if (Outer)
    {
        Outer->RemoveChildObject(this);
    }

    Outer = InOuter;

    if (Outer)
    {
        Outer->AddChildObject(this);
    }
}

void MObject::AddToRoot()
{
    ObjectFlags |= ObjectFlag_RootSet;
    GetRootSet().insert(this);
}

void MObject::RemoveFromRoot()
{
    ObjectFlags &= ~ObjectFlag_RootSet;
    GetRootSet().erase(this);
}

void MObject::AddChildObject(MObject* Child)
{
    if (!Child)
    {
        return;
    }

    for (MObject* ExistingChild : Children)
    {
        if (ExistingChild == Child)
        {
            return;
        }
    }

    Children.push_back(Child);
}

void MObject::RemoveChildObject(MObject* Child)
{
    if (!Child)
    {
        return;
    }

    for (auto It = Children.begin(); It != Children.end(); ++It)
    {
        if (*It == Child)
        {
            Children.erase(It);
            return;
        }
    }
}

MString MObject::ToString() const
{
    MClass* LocalClass = GetClass();
    if (!LocalClass)
    {
        return "MObject{Class=<null>, ObjectId=" + MStringUtil::ToString(GetObjectId()) + "}";
    }

    MString Body = LocalClass->ExportObjectToString(this);
    if (!Name.empty())
    {
        Body += " [Name=\"" + Name + "\"]";
    }
    Body += " [ObjectId=" + MStringUtil::ToString(GetObjectId()) + "]";
    return Body;
}

bool MObject::CallFunction(const MString& InName)
{
    return InvokeFunction<void>(InName, nullptr);
}
