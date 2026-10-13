/**
 * @file Reflection.cpp
 * @brief 实现 MObject 的非模板部分:析构清理、全局对象表与 Root 集、父子挂接、字符串导出入口。
 *
 * @detail 只放不依赖模板的实现;归档读写与函数调用相关的部分以模板/inline 形式放在
 *   ReflectionArchiveInvoke.inl。全局对象表与 Root 集都是"函数内 static":
 *   首次调用时构造、进程退出时析构,于是它们的析构顺序排在普通 MObject 之后 ——
 *   这一事实直接决定了析构函数里不能再去 Root 集摘除自己(见 ~MObject 的 NOTE)。
 */
#include "Common/Runtime/Reflect/Reflection.h"
#include "Common/Runtime/Object/IDisposable.h"

/**
 * @brief 析构:释放可释放资源、脱离父对象、从全局对象表摘除自己,并连带释放子对象。
 *
 * @detail 顺序是有意的:
 *   1. 若实现了 IDisposable 且尚未释放,先 Dispose() —— 给业务侧一个"对象仍完整"的
 *      收尾时机;IsDisposed() 判断兜住"业务已经手动释放过"的情况。
 *   2. SetOuter(nullptr) 断父子关系(父的 Children 中那份引用会被摘除)。
 *   3. 从全局对象表摘除自己,此后 FindObject 再也查不到。
 *   4. 清空 Children,子对象的 TSharedPtr 引用计数归零,由 shared_ptr 自动析构。
 *
 * NOTE: 这里过去会调用 RemoveFromRoot(),现已删除 —— Root 集里的 TSharedPtr<MObject>
 *   与 Service 持有的 shared_ptr 共用同一个控制块,在静态 Root 集上再擦除一次,
 *   会导致进程退出时(__run_exit_handlers)对同一对象二次执行 ~TSharedPtr<MObject>,
 *   把引用计数减到 -1 并 double free。
 *   现在改为"由静态 Root 集自己收尾":程序退出时静态 RootSet 析构,其中每个
 *   TSharedPtr 恰好销毁一次;再加上 MServiceMain::Run 在 Service 侧的释放,
 *   MObject 正好被释放一次。
 */
MObject::~MObject() {
    if (IDisposable* Disposable = dynamic_cast<IDisposable*>(this)) { // 静态类型信息不足以判定,这里用 dynamic_cast
        if (!Disposable->IsDisposed()) {
            Disposable->Dispose();
        }
    }

    SetOuter(nullptr);
    GetObjectMap().erase(ObjectId);

    // TSharedPtr<MObject> 析构链自动 delete child
    Children.clear();
}

/**
 * @brief GetObjectMap - 全局对象表(ObjectId → MObject*),进程内唯一。
 *
 * @detail 函数内 static:构造发生在首次使用时,析构在进程退出时。
 *   存的是裸指针,所有权始终在调用方,TSharedPtr 引用不在这里。
 */
TMap<uint64, MObject*>& MObject::GetObjectMap() {
    static TMap<uint64, MObject*> ObjectMap;
    return ObjectMap;
}

/**
 * @brief GetRootSet - 全局 Root 集,持有对象的一份 TSharedPtr 以防被回收。
 *
 * @detail 同样"函数内 static"。被销毁的顺序决定它不是清空的场所 ——
 *   见 ~MObject 的 NOTE:在 MObject 析构期操作 Root 集会造成二次析构。
 */
TSet<TSharedPtr<MObject>>& MObject::GetRootSet() {
    static TSet<TSharedPtr<MObject>> RootSet;
    return RootSet;
}

/**
 * @brief VisitReferencedObjects - 遍历本对象直接持有的子对象。
 * @param Visitor 访问回调;为空时直接返回
 *
 * @detail 只访问 Children 里的直接子对象,不递归、不回溯 Outer(避免环)。
 *   回调收到的是裸指针,回调期间不要销毁对象。
 */
void MObject::VisitReferencedObjects(const TFunction<void(MObject*)>& Visitor) const {
    if (!Visitor) {
        return;
    }

    for (const TSharedPtr<MObject>& Child : Children) {
        if (Child) {
            Visitor(Child.Get());
        }
    }
}

/**
 * @brief SetOuter - 改父对象,并同步维护新旧父的 Children。
 * @param InOuter 新父对象;传 nullptr 表示脱离父对象
 *
 * @detail 两个方向都要照顾:
 *   - 先从旧父的 Children 里摘掉自己(FindChildShared 找到的是同一控制块的那份引用,
 *     所以摘除不会影响本对象的存活)。
 *   - 再挂到新父:常规路径是 NewMObject 已把自己的 TSharedPtr 放进 Outer->Children,
 *     所以这里先找、找到就不重复插入;找不到(说明是裸调 SetOuter)才兜底插入一份
 *     "哑"shared_ptr —— 它的删除器是空函数,即新父只管引用可达性、不负责释放,
 *     避免出现两个所有者争抢 delete。
 */
void MObject::SetOuter(MObject* InOuter) {
    if (Outer == InOuter) {
        return;
    }

    if (Outer) {
        TSharedPtr<MObject> SelfRef = Outer->FindChildShared(this);
        if (SelfRef.IsValid()) {
            Outer->RemoveChildObject(SelfRef);
        }
    }

    Outer = InOuter;

    if (Outer) {
        // 路径：业务代码 NewMObject 已经把 TSharedPtr 挂到 Outer->Children；
        // 这里不再额外 AddChildObject，避免重复持有。
        // 兜底：如果 Outer 找不到 this 的 TSharedPtr 引用（即 SetOuter 在 NewMObject 之外被裸调），
        // 创建一个 aliased shared_ptr，custom deleter 不再 delete（Outer 不管生命周期）。
        TSharedPtr<MObject> SelfRef = Outer->FindChildShared(this);
        if (!SelfRef.IsValid()) {
            Outer->AddChildObject(TSharedPtr<MObject>(this, [](MObject*) {}));
        }
    }
}

/**
 * @brief AddToRootSet - 置 Root 标志并把给定引用插入 Root 集。
 * @param Object 要加入 Root 集的对象;无效对象直接返回
 *
 * @detail 这是 Root 集唯一的插入入口(NewMObject 调用)。
 *   注意插入的是调用方传进来的那份 TSharedPtr:控制块不一致会导致之后摘不掉。
 */
void MObject::AddToRootSet(const TSharedPtr<MObject>& Object) {
    if (!Object.IsValid()) {
        return;
    }
    Object->ObjectFlags |= ObjectFlag_RootSet;
    GetRootSet().insert(Object);
}

/**
 * @brief AddToRoot - 置 Root 标志,并尝试把已有的 TSharedPtr 引用插入 Root 集。
 *
 * @detail 引用从自己的 Children 里找 —— 这在语义上很绕:只有"自己已被别人持有时"
 *   才找得到。找不到时只是置了标志位,Root 集里并没有引用。
 *   新代码应当改用 AddToRootSet(能明确传入引用)。
 */
void MObject::AddToRoot() {
    ObjectFlags |= ObjectFlag_RootSet;
    // TSharedPtr 由 NewMObject 在调用 AddToRoot 之前/之后 push 到 RootSet。
    // 直接调 AddToRoot() 的旧调用点需要先在外面 MakeShared 持有。
    TSharedPtr<MObject> Existing = FindChildShared(this);
    if (Existing.IsValid()) {
        GetRootSet().insert(Existing);
    }
}

/** @brief RemoveFromRoot - 清 Root 标志并从 Root 集移除引用;必须用同一控制块的引用才能摘干净。 */
void MObject::RemoveFromRoot() {
    ObjectFlags &= ~ObjectFlag_RootSet;
    TSharedPtr<MObject> Existing = FindRootShared(this);
    if (Existing.IsValid()) {
        GetRootSet().erase(Existing);
    }
}

/**
 * @brief AddChildObject - 把一个子对象引用追加到 Children(按裸指针去重)。
 * @param Child 子对象引用;无效则直接返回
 *
 * @detail 幂等:已存在指向同一对象的引用时不再重复插入。
 *   这里不做"子对象已有父"的校验,重复挂到多个父下不会被阻止。
 */
void MObject::AddChildObject(const TSharedPtr<MObject>& Child) {
    if (!Child.IsValid()) {
        return;
    }

    for (const TSharedPtr<MObject>& ExistingChild : Children) {
        if (ExistingChild.Get() == Child.Get()) {
            return;
        }
    }

    Children.push_back(Child);
}

/**
 * @brief RemoveChildObject - 从 Children 里摘掉指向同一对象的引用。
 * @param Child 目标子对象;无效则直接返回
 *
 * @detail 比较的是裸指针而非控制块:即使传入的是别名引用也能摘对。
 *   线性查找,Children 通常很小。
 */
void MObject::RemoveChildObject(const TSharedPtr<MObject>& Child) {
    if (!Child.IsValid()) {
        return;
    }

    for (auto It = Children.begin(); It != Children.end(); ++It) {
        if (It->Get() == Child.Get()) {
            Children.erase(It);
            return;
        }
    }
}

/** @brief FindChildShared - 在自己的 Children 中找出指向 Child 的引用;找不到返回空。 */
TSharedPtr<MObject> MObject::FindChildShared(MObject* Child) const {
    if (!Child) {
        return nullptr;
    }
    for (const TSharedPtr<MObject>& ExistingChild : Children) {
        if (ExistingChild.Get() == Child) {
            return ExistingChild;
        }
    }
    return nullptr;
}

/** @brief FindRootShared - 在全局 Root 集中找出指向 Object 的引用;找不到返回空。 */
TSharedPtr<MObject> MObject::FindRootShared(MObject* Object) const {
    auto& RootSet = GetRootSet();
    for (const TSharedPtr<MObject>& Existing : RootSet) {
        if (Existing.Get() == Object) {
            return Existing;
        }
    }
    return nullptr;
}

/**
 * @brief ToString - 渲染对象的反射字段,并附上 Name 与 ObjectId。
 * @return 形如 "<字段…> [Name=…] [ObjectId=…]";没有类元数据时返回只含 id 的占位串
 *
 * @detail 字段部分由 MClass::ExportObjectToString 产出,所以对象显示得全不全面
 *   取决于它的反射属性注册了什么。Name 为空时省略该段,便于日志阅读。
 */
MString MObject::ToString() const {
    MClass* LocalClass = GetClass();
    if (!LocalClass) {
        return "MObject{Class=<null>, ObjectId=" + MStringUtil::ToString(GetObjectId()) + "}";
    }

    MString Body = LocalClass->ExportObjectToString(this);
    if (!Name.empty()) {
        Body += " [Name=\"" + Name + "\"]";
    }
    Body += " [ObjectId=" + MStringUtil::ToString(GetObjectId()) + "]";
    return Body;
}

/**
 * @brief CallFunction - 按名字调用无参、无返回值的反射函数。
 * @param InName 函数名
 * @return true 表示找到函数且调用成功
 *
 * @detail 只是 InvokeFunction<void> 的语法糖;实现见
 *   ReflectionArchiveInvoke.inl(那里才知道 MFunction 的调用约定)。
 */
bool MObject::CallFunction(const MString& InName) {
    return InvokeFunction<void>(InName, nullptr);
}
