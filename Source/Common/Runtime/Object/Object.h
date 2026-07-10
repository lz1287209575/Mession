#pragma once

// 统一对象入口：
// 反射能力与对象身份由 Common/Runtime/Reflect/Reflection.h 中的 MObject 提供。
#include "Common/Runtime/Reflect/Reflection.h"

/**
 * NewMObject — MObject 派生类的唯一构造入口。
 *
 * 返回 TSharedPtr<TObject>：
 *   - 内部 MakeShared 持有所有权
 *   - 挂到 Outer->Children（Outer 也持一份 TSharedPtr）
 *   - Outer 为空时挂到 RootSet
 *
 * 业务代码持有返回的 TSharedPtr 即可控制生命周期：
 *   auto* Profile = NewMObject<MPlayerProfile>(this, "Profile");
 *   // 块结束 RAII 释放；如已挂到 Outer，Outer 也持一份
 */
template<typename TObject, typename... TArgs>
TSharedPtr<TObject> NewMObject(MObject* Outer, const MString& Name = "", TArgs&&... Args)
{
    static_assert(std::is_base_of_v<MObject, TObject>, "TObject must derive from MObject");

    TSharedPtr<TObject> Object = MakeShared<TObject>(std::forward<TArgs>(Args)...);
    Object->SetClass(TObject::StaticClass());
    Object->SetName(Name);

    if (Outer)
    {
        // Outer 持有 TSharedPtr，this 端 SetOuter 走 FindChildShared 找到 Outer 那份引用
        Outer->AddChildObject(Object);
        Object->SetOuter(Outer);
    }
    else
    {
        // 无 Outer：挂到 RootSet，进程级生命周期
        Object->SetOuter(nullptr);
        MObject::AddToRootSet(Object);
    }

    return Object;
}

/**
 * CreateDefaultSubObject — UE 风格的默认子对象（生命周期跟随 Owner）。
 */
template<typename TObject, typename... TArgs>
TSharedPtr<TObject> CreateDefaultSubObject(MObject* Owner, const MString& Name = "", TArgs&&... Args)
{
    TSharedPtr<TObject> Object = NewMObject<TObject>(Owner, Name, std::forward<TArgs>(Args)...);
    Object->MarkAsDefaultSubObject();
    return Object;
}

// DestroyMObject 已删除——MObject 生命周期由 TSharedPtr 持有自动管理。
// 业务代码改用 TSharedPtr<MObject> 持有，块结束自然释放。

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
