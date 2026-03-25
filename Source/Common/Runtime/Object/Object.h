#pragma once

// 统一对象入口：
// 反射能力与对象身份由 Common/Runtime/Reflect/Reflection.h 中的 MObject 提供。
#include "Common/Runtime/Reflect/Reflection.h"

template<typename TObject, typename... TArgs>
TObject* NewMObject(MObject* Outer, const MString& Name = "", TArgs&&... Args)
{
    static_assert(std::is_base_of_v<MObject, TObject>, "TObject must derive from MObject");

    TObject* Object = new TObject(std::forward<TArgs>(Args)...);
    Object->SetClass(TObject::StaticClass());
    Object->SetName(Name);
    if (Outer)
    {
        Object->SetOuter(Outer);
    }
    else
    {
        Object->AddToRoot();
    }
    return Object;
}

template<typename TObject, typename... TArgs>
TObject* CreateDefaultSubObject(MObject* Owner, const MString& Name = "", TArgs&&... Args)
{
    TObject* Object = NewMObject<TObject>(Owner, Name, std::forward<TArgs>(Args)...);
    Object->MarkAsDefaultSubObject();
    return Object;
}

inline void DestroyMObject(MObject* Object)
{
    delete Object;
}

template<typename TVisitor>
void ForEachObjectInSubtree(MObject* Root, TVisitor&& Visitor)
{
    if (!Root)
    {
        return;
    }

    TSet<uint64> Visited;
    TFunction<void(MObject*)> Walk = [&](MObject* Object)
    {
        if (!Object)
        {
            return;
        }

        if (Visited.count(Object->GetObjectId()) > 0)
        {
            return;
        }

        Visited.insert(Object->GetObjectId());
        Visitor(Object);
        Object->VisitReferencedObjects(Walk);
    };

    Walk(Root);
}
