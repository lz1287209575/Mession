/**
 * @file Object.h
 * @brief 对象构造入口:NewMObject / CreateDefaultSubObject 与子对象遍历。
 *
 * @detail 反射能力与对象身份由 Common/Runtime/Reflect/Reflection.h 中的
 *   MObject 提供;本头文件只约定"对象怎么造、造完挂到哪里、生命期归谁"。
 */
#pragma once

#include "Common/Runtime/Reflect/Reflection.h"

/**
 * @brief NewMObject - MObject 派生类的唯一构造入口。
 * @param Outer 父对象;为空表示无父(对象挂到 RootSet)
 * @param Name 对象名;可为空
 * @param Args 转发给 TObject 构造函数的参数
 * @return 新对象的 TSharedPtr,所有权归返回值(以及后续可能的其他持有者)
 *
 * @detail 生命期与挂载点:
 *   - 所有权始终在 MakeShared 的返回 TSharedPtr 上:返回值被丢弃(且无其他
 *     持有者)时对象立即析构
 *   - Outer 非空:调用 SetOuter,后者会向 Outer->Children 登记一份**非拥有**
 *     的 TSharedPtr(deleter 为空),因此 Outer 不会延长对象寿命
 *   - Outer 为空:置空 Outer 并调用 MObject::AddToRootSet,由全局 RootSet
 *     持有一份强引用,使裸指针调用方不会拿到悬空对象
 *
 * 用法:
 *   auto Profile = NewMObject<MPlayerProfile>(this, "Profile");
 *   // 返回值析构时对象释放;若指定了 Outer,Outer->Children 只留非拥有引用
 */
template <typename TObject, typename... TArgs> TSharedPtr<TObject> NewMObject(MObject* Outer, const MString& Name = "", TArgs&&... Args) {
    static_assert(std::is_base_of_v<MObject, TObject>, "TObject must derive from MObject");

    TSharedPtr<TObject> Object = MakeShared<TObject>(std::forward<TArgs>(Args)...);
    Object->SetClass(TObject::StaticClass());
    Object->SetName(Name);

    if (Outer) {
        // SetOuter 内部会尝试从 Outer->Children 找 this 的现有引用;找不到时
        // 才登记一个空 deleter 的别名 TSharedPtr(即"只登记不拥有")。
        Object->SetOuter(Outer);
    } else {
        // 无父对象:交给全局 RootSet 托管,保证进程内始终有强引用。
        Object->SetOuter(nullptr);
        MObject::AddToRootSet(Object);
    }

    return Object;
}

/**
 * @brief CreateDefaultSubObject - UE 风格的默认子对象(生命期跟随 Owner)。
 * @param Owner 拥有者;由它接收子对象
 * @param Name 子对象名
 * @param Args 转发给 TObject 构造函数的参数
 * @return 新对象的 TSharedPtr
 *
 * @detail 只是 NewMObject 的包装,额外调用 MarkAsDefaultSubObject() 打上
 *   DefaultSubObject 标志,供上层区分"构造期默认子对象"与业务动态创建的对象。
 */
template <typename TObject, typename... TArgs> TSharedPtr<TObject> CreateDefaultSubObject(MObject* Owner, const MString& Name = "", TArgs&&... Args) {
    TSharedPtr<TObject> Object = NewMObject<TObject>(Owner, Name, std::forward<TArgs>(Args)...);
    Object->MarkAsDefaultSubObject();
    return Object;
}

// DestroyMObject 已删除——MObject 生命周期由 TSharedPtr 持有自动管理。
// 业务代码改用 TSharedPtr<MObject> 持有，块结束自然释放。

/**
 * @brief ForEachObjectInSubtree - 深度优先遍历对象引用图,对每个对象调用一次 Visitor。
 * @param Root 遍历起点;为空则什么都不做
 * @param Visitor 形如 void(MObject*) 的可调用对象
 *
 * @detail 遍历的是 MObject::VisitReferencedObjects 暴露的引用图(当前实现即
 *   Children 子对象),而不是 Outer 链;按 ObjectId 去重,因此即使引用图成环
 *   或同一对象被多处引用也只访问一次。遍历顺序为先访问自身再递归子对象,
 *   且 Visitor 内部不应修改被遍历的引用关系(无迭代保护)。
 */
template <typename TVisitor> void ForEachObjectInSubtree(MObject* Root, TVisitor&& Visitor) {
    if (!Root) {
        return;
    }

    TSet<uint64>              Visited;
    TFunction<void(MObject*)> Walk = [&](MObject* Object) {
        if (!Object) {
            return;
        }

        if (Visited.count(Object->GetObjectId()) > 0) {
            return;
        }

        Visited.insert(Object->GetObjectId());
        Visitor(Object);
        Object->VisitReferencedObjects(Walk);
    };

    Walk(Root);
}
