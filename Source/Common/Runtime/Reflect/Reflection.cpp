#include "Common/Runtime/Reflect/Reflection.h"
#include "Common/Runtime/Object/IDisposable.h"

MObject::~MObject() {
    // NOTE: We previously called RemoveFromRoot() here, but the TSharedPtr<MObject>
    // held inside GetRootSet() shares the same control block as the Service's
    // holding shared_ptr. Erasing it from the static RootSet triggers a second
    // ~TSharedPtr<MObject> destructor on shutdown (via __run_exit_handlers),
    // which decrements ref_count to -1 and double-frees the object.
    //
    // Removal from RootSet is now handled implicitly: when the static RootSet
    // itself is destroyed at program exit, each TSharedPtr<MObject> it holds
    // is destroyed once. Combined with the Service-side release in
    // MServiceMain::Run, the MObject is disposed exactly once.

    if (IDisposable* Disposable = dynamic_cast<IDisposable*>(this)) {
        if (!Disposable->IsDisposed()) {
            Disposable->Dispose();
        }
    }

    SetOuter(nullptr);
    GetObjectMap().erase(ObjectId);

    // TSharedPtr<MObject> 析构链自动 delete child
    Children.clear();
}

TMap<uint64, MObject*>& MObject::GetObjectMap() {
    static TMap<uint64, MObject*> ObjectMap;
    return ObjectMap;
}

TSet<TSharedPtr<MObject>>& MObject::GetRootSet() {
    static TSet<TSharedPtr<MObject>> RootSet;
    return RootSet;
}

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

void MObject::AddToRootSet(const TSharedPtr<MObject>& Object) {
    if (!Object.IsValid()) {
        return;
    }
    Object->ObjectFlags |= ObjectFlag_RootSet;
    GetRootSet().insert(Object);
}

void MObject::AddToRoot() {
    ObjectFlags |= ObjectFlag_RootSet;
    // TSharedPtr 由 NewMObject 在调用 AddToRoot 之前/之后 push 到 RootSet。
    // 直接调 AddToRoot() 的旧调用点需要先在外面 MakeShared 持有。
    TSharedPtr<MObject> Existing = FindChildShared(this);
    if (Existing.IsValid()) {
        GetRootSet().insert(Existing);
    }
}

void MObject::RemoveFromRoot() {
    ObjectFlags &= ~ObjectFlag_RootSet;
    TSharedPtr<MObject> Existing = FindRootShared(this);
    if (Existing.IsValid()) {
        GetRootSet().erase(Existing);
    }
}

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

TSharedPtr<MObject> MObject::FindRootShared(MObject* Object) const {
    auto& RootSet = GetRootSet();
    for (const TSharedPtr<MObject>& Existing : RootSet) {
        if (Existing.Get() == Object) {
            return Existing;
        }
    }
    return nullptr;
}

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

bool MObject::CallFunction(const MString& InName) {
    return InvokeFunction<void>(InName, nullptr);
}
