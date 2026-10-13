/**
 * @file Class.cpp
 * @brief MClass 实现:实例构造/销毁 + 属性与函数的继承链查找 + 快照读写。
 *
 * @detail 继承链约定:查找"本类优先、再回溯父类";序列化"先父后子"。
 *   两个方向都靠递归实现,因此查找与快照的顺序在读写两侧严格对称,
 *   这也是快照负载能按字段顺序解析的前提。
 */
#include "Common/Runtime/Reflect/Reflection.h"

/** @brief 分配本进程内唯一的 ClassId(注册顺序自增,1 起)。 */
MClass::MClass() {
    ClassId = ++GlobalClassId;
}

/** @brief 释放本类拥有的全部属性与函数描述对象。 */
MClass::~MClass() {
    for (MProperty* Prop : Properties) {
        delete Prop;
    }
    for (MFunction* Func : Functions) {
        delete Func;
    }
}

/**
 * @brief 创建实例并绑定反射类。
 * @return 新实例;未设置 Constructor 时返回 nullptr
 *
 * @detail 只有非 Struct 类别才做 static_cast<MObject*> + SetClass;Struct 没有
 *   MObject 头,强制转换会读到不属于它的内存。
 */
void* MClass::CreateInstance() const {
    if (!Constructor) {
        return nullptr;
    }

    void* Obj = Constructor(nullptr);

    auto* ReflectObj = (ClassKind != EClassKind::Struct) ? static_cast<MObject*>(Obj) : nullptr;
    if (ReflectObj) {
        ReflectObj->SetClass(const_cast<MClass*>(this));
    }

    return Obj;
}

/** @brief 用注册的析构跳板销毁实例;未设置 Destructor 或对象为空时什么都不做。 */
void MClass::DestroyInstance(void* Object) const {
    if (!Object || !Destructor) {
        return;
    }

    Destructor(Object);
}

/**
 * @brief 在调用方提供的内存上原地构造(placement new)并绑定反射类。
 * @param Object 已分配的内存;为空或未设置 Constructor 时什么都不做
 */
void MClass::Construct(void* Object) const {
    if (!Constructor || !Object) {
        return;
    }

    Constructor(Object);

    auto* ReflectObj = (ClassKind != EClassKind::Struct) ? static_cast<MObject*>(Object) : nullptr;
    if (ReflectObj) {
        ReflectObj->SetClass(const_cast<MClass*>(this));
    }
}

/**
 * @brief 按名字查找属性。
 * @param InName 属性名
 * @return 命中属性;本类优先,未命中回溯父类,都没有返回 nullptr
 *
 * @detail 线性扫描(O(本类属性数)),热路径上应缓存结果。
 */
MProperty* MClass::FindProperty(const MString& InName) const {
    for (MProperty* Prop : Properties) {
        if (Prop && Prop->Name == InName) {
            return Prop;
        }
    }

    if (ParentClass) {
        return ParentClass->FindProperty(InName);
    }

    return nullptr;
}

/**
 * @brief 按属性 id 查找(PropertyId 为注册顺序编号,仅进程内有效)。
 * @param InId 属性 id
 * @return 命中属性;本类优先,再回溯父类
 */
MProperty* MClass::FindPropertyById(uint16 InId) const {
    for (MProperty* Prop : Properties) {
        if (Prop && Prop->PropertyId == InId) {
            return Prop;
        }
    }

    if (ParentClass) {
        return ParentClass->FindPropertyById(InId);
    }

    return nullptr;
}

/**
 * @brief 按资产字段 id 查找(与字段名绑定的稳定哈希,可跨进程使用)。
 * @param InAssetFieldId ComputeStableAssetFieldId 的结果
 * @return 命中属性;本类优先,再回溯父类
 */
MProperty* MClass::FindPropertyByAssetFieldId(uint32 InAssetFieldId) const {
    for (MProperty* Prop : Properties) {
        if (Prop && Prop->AssetFieldId == InAssetFieldId) {
            return Prop;
        }
    }

    if (ParentClass) {
        return ParentClass->FindPropertyByAssetFieldId(InAssetFieldId);
    }

    return nullptr;
}

/**
 * @brief 按元数据键值查找属性。
 * @param InKey 元数据键
 * @param InValue 期望值;为空串时"存在该键"即命中
 * @return 命中属性;本类优先,再回溯父类
 */
MProperty* MClass::FindPropertyByMetadata(const MString& InKey, const MString& InValue) const {
    for (MProperty* Prop : Properties) {
        if (!Prop)
            continue;
        if (const MString* Val = Prop->FindMetadata(InKey)) {
            if (InValue.empty() || *Val == InValue) {
                return Prop;
            }
        }
    }
    if (ParentClass)
        return ParentClass->FindPropertyByMetadata(InKey, InValue);
    return nullptr;
}

/**
 * @brief 按名字查找函数。
 * @param InName 函数名
 * @return 命中函数;本类优先,再回溯父类
 *
 * @detail 同名派生函数会遮住父类实现(反射层不做重载解析)。
 */
MFunction* MClass::FindFunction(const MString& InName) const {
    for (MFunction* Func : Functions) {
        if (Func && Func->Name == InName) {
            return Func;
        }
    }

    if (ParentClass) {
        return ParentClass->FindFunction(InName);
    }

    return nullptr;
}

/**
 * @brief 按 FunctionId 查找函数(线上协议使用的稳定 id 通道)。
 * @param InId 函数 id
 * @return 命中函数;本类优先,再回溯父类
 */
MFunction* MClass::FindFunctionById(uint16 InId) const {
    for (MFunction* Func : Functions) {
        if (Func && Func->FunctionId == InId) {
            return Func;
        }
    }

    if (ParentClass) {
        return ParentClass->FindFunctionById(InId);
    }

    return nullptr;
}

/**
 * @brief 把对象的全部属性写入 archive(先父类、后本类)。
 * @param Object 对象实例;为空直接返回
 * @param Ar 目标 archive(写模式)
 *
 * @detail 递归顺序必须与 ReadSnapshotFields 完全一致,否则负载会字段错位;
 *   取消引用父类会少写基类字段,导致反序列化时后续字段整体偏移。
 */
void MClass::WriteSnapshot(void* Object, MReflectArchive& Ar) const {
    if (!Object) {
        return;
    }

    // 继承序列化(2026-08-14 actor-member-framework §6):先父后子递归——
    // 与 C++ 单继承对象布局一致(基类子对象在前),保证基类字段(如
    // FPlayerRequestBase::PlayerId)落盘、反序列化往返一致。
    if (ParentClass) {
        ParentClass->WriteSnapshot(Object, Ar);
    }

    for (MProperty* Prop : Properties) {
        if (!Prop) {
            continue;
        }
        Prop->WriteValue(Object, Ar);
    }
}

/**
 * @brief 只把命中域掩码的属性写入 archive(先父类、后本类)。
 * @param Object 对象实例;为空直接返回
 * @param Ar 目标 archive
 * @param InDomainMask 域位掩码;为 0 时直接返回(不写任何字段)
 *
 * @detail 读取侧要用同样的掩码,两侧过滤条件必须一致((DomainFlags & Mask) != 0)。
 */
void MClass::WriteSnapshotByDomain(void* Object, MReflectArchive& Ar, uint64 InDomainMask) const {
    if (!Object || InDomainMask == 0) {
        return;
    }

    if (ParentClass) {
        ParentClass->WriteSnapshotByDomain(Object, Ar, InDomainMask);
    }

    for (MProperty* Prop : Properties) {
        if (!Prop || (Prop->DomainFlags & InDomainMask) == 0) {
            continue;
        }
        Prop->WriteValue(Object, Ar);
    }
}

/**
 * @brief 由字节负载读回对象(建 archive 后转调 ReadSnapshotFields)。
 * @param Object 对象实例;为空直接返回
 * @param Data 写入侧产出的负载
 *
 * @detail 负载多余或不足都不会在这里报错,只体现在 Ar.bReadOverflow /
 *   ReadPos 上,需要严格校验的调用方应自行检查(参见
 *   MObjectDomainUtils::ApplyObjectDomainSnapshotRecords)。
 */
void MClass::ReadSnapshot(void* Object, const TByteArray& Data) const {
    if (!Object) {
        return;
    }

    MReflectArchive Ar(Data);
    ReadSnapshotFields(Object, Ar);
}

/**
 * @brief 读回字段(父类与本类共用调用方传入的 archive 游标)。
 * @param Object 对象实例;为空直接返回
 * @param Ar 读模式 archive;游标由父类到本类单向推进
 *
 * @detail 之所以不递归调 ReadSnapshot(Object, Data),是因为那会为父类重建
 *   archive,游标归零后本类字段会从负载开头重读,造成字段错位。
 */
void MClass::ReadSnapshotFields(void* Object, MReflectArchive& Ar) const {
    if (!Object) {
        return;
    }

    if (ParentClass) {
        ParentClass->ReadSnapshotFields(Object, Ar);
    }

    for (MProperty* Prop : Properties) {
        if (!Prop) {
            continue;
        }
        Prop->WriteValue(Object, Ar);
    }
}

/**
 * @brief 由字节负载读回指定域的字段。
 * @param Object 对象实例;为空直接返回
 * @param Data 写入侧产出的域负载
 * @param InDomainMask 域位掩码;为 0 时直接返回
 */
void MClass::ReadSnapshotByDomain(void* Object, const TByteArray& Data, uint64 InDomainMask) const {
    if (!Object || InDomainMask == 0) {
        return;
    }

    MReflectArchive Ar(Data);

    if (ParentClass) {
        ParentClass->ReadSnapshotFieldsByDomain(Object, Ar, InDomainMask);
    }

    for (MProperty* Prop : Properties) {
        if (!Prop || (Prop->DomainFlags & InDomainMask) == 0) {
            continue;
        }
        Prop->WriteValue(Object, Ar);
    }
}

/**
 * @brief 读回指定域字段的游标共享版本(供父类递归复用同一个 archive)。
 * @param Object 对象实例;为空直接返回
 * @param Ar 读模式 archive
 * @param InDomainMask 域位掩码;为 0 时直接返回
 */
void MClass::ReadSnapshotFieldsByDomain(void* Object, MReflectArchive& Ar, uint64 InDomainMask) const {
    if (!Object || InDomainMask == 0) {
        return;
    }

    if (ParentClass) {
        ParentClass->ReadSnapshotFieldsByDomain(Object, Ar, InDomainMask);
    }

    for (MProperty* Prop : Properties) {
        if (!Prop || (Prop->DomainFlags & InDomainMask) == 0) {
            continue;
        }
        Prop->WriteValue(Object, Ar);
    }
}

/**
 * @brief 渲染 "类名{字段=值, ...}" 形式的调试文本。
 * @param Object 待渲染对象;为空返回 "类名{<null>}"
 * @return 文本;字段值由 MProperty::ExportValueToString 决定
 *
 * @detail 只输出直接父类的自有属性 + 本类自有属性(不递归更上层祖先),
 *   父类字段排在本类之前;两者都没有时得到 "类名{}"。纯调试用途,
 *   文本格式不构成稳定接口。
 */
MString MClass::ExportObjectToString(const void* Object) const {
    if (!Object) {
        return ClassName + "{<null>}";
    }

    MString Result = ClassName + "{";
    bool    bFirst = true;
    if (ParentClass) {
        // 只取直接父类的自有属性:更上层祖先的字段不在此输出(与
        // WriteSnapshot 的完整递归不同),深继承链的调试文本会缺字段。
        const TVector<MProperty*>& ParentProperties = ParentClass->GetProperties();
        for (const MProperty* Prop : ParentProperties) {
            if (!Prop) {
                continue;
            }

            if (!bFirst) {
                Result += ", ";
            }
            Result += Prop->Name + "=" + Prop->ExportValueToString(Object);
            bFirst = false;
        }
    }

    for (const MProperty* Prop : Properties) {
        if (!Prop) {
            continue;
        }

        if (!bFirst) {
            Result += ", ";
        }
        Result += Prop->Name + "=" + Prop->ExportValueToString(Object);
        bFirst = false;
    }

    Result += "}";
    return Result;
}

/**
 * @brief 按字段字节复制属性值(浅拷贝)。
 * @param Dest 目标对象;为空直接返回
 * @param Src 源对象;为空直接返回
 *
 * @detail 只复制本类自有属性(不含父类),且是 memcpy 级别的字节拷贝:
 *   对含堆缓冲的属性(MString/TVector 等)会导致两个对象共享内部指针,
 *   因此仅适用于属性全为平凡类型的场合。取地址失败或 Size==0 的属性跳过。
 */
void MClass::CopyProperties(void* Dest, const void* Src) const {
    if (!Dest || !Src) {
        return;
    }

    for (MProperty* Prop : Properties) {
        if (!Prop || Prop->Size == 0) {
            continue;
        }

        void*       DestPtr = Prop->GetValueVoidPtr(Dest);
        const void* SrcPtr  = Prop->GetValueVoidPtr(Src);
        if (!DestPtr || !SrcPtr) {
            continue;
        }
        memcpy(DestPtr, SrcPtr, Prop->Size);
    }
}
