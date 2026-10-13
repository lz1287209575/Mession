/**
 * @file ObjectDomainUtils.h
 * @brief 对象域快照工具:按属性域(复制/持久化/资产)抓取与回放对象树状态。
 *
 * @detail 快照只覆盖与指定域有交集的属性,并以"根对象 id + 相对对象路径 + 类名"
 *   描述每个被捕获的对象,因此回放时需要一棵结构相同的对象树(见
 *   ApplyObjectDomainSnapshotRecords)。所有函数都是 inline 头文件实现,
 *   不做任何加锁:调用方需保证遍历期间对象树不被并发修改。
 */
#pragma once

#include "Common/Runtime/Reflect/Reflection.h"

/**
 * @brief 单个对象的域快照记录。
 *
 * @detail 不保存指针:回放时靠 RootObjectId + ObjectPath 重新定位对象,
 *   ClassName 用于回放前的类型校验(ObjectPath 命中但类不同则拒绝写回)。
 */
struct SObjectDomainSnapshotRecord {
    uint64     RootObjectId = 0; // 抓取时的根对象 ObjectId(仅作标识,回放不校验)
    uint64     ObjectId     = 0; // 被捕获对象的 ObjectId(仅作标识)
    uint16     ClassId      = 0; // 被捕获对象的类 id
    MString    ObjectPath;       // 相对根的路径,以 '.' 分隔对象名;根为空串
    MString    ClassName;        // 被捕获对象的类名,回放时用于校验
    TByteArray SnapshotData;     // 该对象的域字段负载(与类字段顺序对应)
};

namespace MObjectDomainUtils {
    /**
     * @brief 递归写入命中域掩码的属性(父类在前,与本类字段顺序一致)。
     * @param InClass 起始类;与 InObject 任一为空、或掩码为 0 时直接返回
     * @param InObject 对象实例
     * @param Archive 目标 archive(写模式)
     * @param InDomainMask 域位掩码(ToMask(EPropertyDomainFlags) 的结果)
     *
     * @detail 只有 (Prop->DomainFlags & InDomainMask) != 0 的属性会落盘,
     *   因此同一棵树上不同域的快照互不干扰。
     */
    inline void WriteClassDomainSnapshotRecursive(const MClass* InClass, void* InObject, MReflectArchive& Archive, uint64 InDomainMask) {
        if (!InClass || !InObject || InDomainMask == 0) {
            return;
        }

        WriteClassDomainSnapshotRecursive(InClass->GetParentClass(), InObject, Archive, InDomainMask);
        for (const MProperty* Prop : InClass->GetProperties()) {
            if (!Prop || (Prop->DomainFlags & InDomainMask) == 0) {
                continue;
            }

            Prop->WriteValue(InObject, Archive);
        }
    }

    /**
     * @brief 递归读回命中域掩码的属性,顺序与写入侧完全一致。
     * @param InClass 起始类;与 InObject 任一为空、或掩码为 0 时视为成功
     * @param InObject 对象实例
     * @param Archive 源 archive(读模式,同时承载游标与溢出标志)
     * @param InDomainMask 域位掩码
     * @return true 全部读回;false 表示 archive 提前耗尽(bReadOverflow)
     *
     * @detail 这里同样调用 MProperty::WriteValue —— 序列化方向由 MReflectArchive
     *   自身决定(bReading/bWriting),属性侧只提供"对称的一次交换",
     *   函数名的 Read 是站在对象角度说的。
     */
    inline bool ReadClassDomainSnapshotRecursive(const MClass* InClass, void* InObject, MReflectArchive& Archive, uint64 InDomainMask) {
        if (!InClass || !InObject || InDomainMask == 0) {
            return true;
        }

        if (!ReadClassDomainSnapshotRecursive(InClass->GetParentClass(), InObject, Archive, InDomainMask)) {
            return false;
        }

        for (const MProperty* Prop : InClass->GetProperties()) {
            if (!Prop || (Prop->DomainFlags & InDomainMask) == 0) {
                continue;
            }

            Prop->WriteValue(InObject, Archive);
            if (Archive.bReadOverflow) {
                return false;
            }
        }

        return true;
    }

    /**
     * @brief 沿继承链判断该类(含父类)是否存在属于指定域的属性。
     * @param InClass 起始类;可为空
     * @param InDomain 目标域
     * @return true 至少有一个属性命中
     *
     * @detail 用于过滤"整类都与该域无关"的对象,避免为它们生成空记录。
     */
    inline bool ClassHasDomainProperties(const MClass* InClass, EPropertyDomainFlags InDomain) {
        for (const MClass* Current = InClass; Current; Current = Current->GetParentClass()) {
            for (const MProperty* Prop : Current->GetProperties()) {
                if (Prop && Prop->HasAnyDomains(InDomain)) {
                    return true;
                }
            }
        }

        return false;
    }

    /**
     * @brief 判断一个对象是否应被纳入本次快照。
     * @param InObject 待判断对象;为空返回 false
     * @param InDomain 目标域
     * @param bOnlyDirty true 时要求该对象至少有一个命中域的脏属性
     * @return true 需要捕获
     */
    inline bool ShouldCaptureObject(MObject* InObject, EPropertyDomainFlags InDomain, bool bOnlyDirty) {
        if (!InObject) {
            return false;
        }

        MClass* ObjectClass = InObject->GetClass();
        if (!ObjectClass || !ClassHasDomainProperties(ObjectClass, InDomain)) {
            return false;
        }

        return !bOnlyDirty || InObject->HasAnyDirtyPropertyForDomain(InDomain);
    }

    /**
     * @brief 抓取单个对象在指定域上的字段负载。
     * @param InObject 目标对象
     * @param InDomain 目标域
     * @param OutData 输出负载(先 clear)
     * @param bOnlyDirty 透传给 ShouldCaptureObject
     * @return true 已生成负载;false 表示该对象不需要捕获(OutData 为空)
     *
     * @detail 本函数不做 bOnlyDirty 的二次判定,由 ShouldCaptureObject 统一决定;
     *   Archive 在这里是新建的写模式实例,负载即 MReflectArchive::Data。
     */
    inline bool BuildObjectDomainSnapshot(MObject* InObject, EPropertyDomainFlags InDomain, TByteArray& OutData, bool bOnlyDirty = false) {
        OutData.clear();
        if (!ShouldCaptureObject(InObject, InDomain, bOnlyDirty)) {
            return false;
        }

        MReflectArchive Archive;
        WriteClassDomainSnapshotRecursive(InObject->GetClass(), InObject, Archive, ToMask(InDomain));
        OutData = std::move(Archive.Data);
        return true;
    }

    /**
     * @brief 遍历对象树,对每个"应捕获"的对象调用 Visitor,并回传其相对路径。
     * @param InRoot 遍历起点;为空直接返回
     * @param InDomain 目标域
     * @param bOnlyDirty 透传给 ShouldCaptureObject(只访问有脏属性的对象)
     * @param Visitor 形如 void(MObject*, const MString&) 的可调用对象;第二参为
     *   相对根的路径(根自身为空串,子对象为 "A"、"A.B")
     *
     * @detail 只沿 Outer/Children 的父子关系向下走(不含引用图),不做环检测:
     *   正常情况下对象树无环,若人为构造出环会无限递归。路径分隔符固定为 '.'。
     */
    template <typename TVisitor> inline void ForEachObjectInDomainSubtree(MObject* InRoot, EPropertyDomainFlags InDomain, bool bOnlyDirty, TVisitor&& Visitor) {
        if (!InRoot) {
            return;
        }

        TFunction<void(MObject*, const MString&)> VisitSubtree;
        VisitSubtree = [&](MObject* Current, const MString& CurrentPath) {
            if (!Current) {
                return;
            }

            if (ShouldCaptureObject(Current, InDomain, bOnlyDirty)) {
                Visitor(Current, CurrentPath);
            }

            for (MObject* Child : Current->GetChildren()) {
                if (!Child) {
                    continue;
                }

                const MString ChildPath = CurrentPath.empty() ? Child->GetName() : (CurrentPath + "." + Child->GetName());
                VisitSubtree(Child, ChildPath);
            }
        };

        VisitSubtree(InRoot, "");
    }

    /**
     * @brief 抓取整棵树在指定域上的记录列表。
     * @param InRoot 根对象;为空返回空列表
     * @param InDomain 目标域
     * @param bOnlyDirty true 时只抓有脏属性的对象
     * @return 记录列表(顺序 = 深度优先顺序,根在前)
     *
     * @detail 每条记录都会重写 RootObjectId(取自 InRoot),因此记录可脱离
     *   InRoot 传递;内层 BuildObjectDomainSnapshot 固定传 bOnlyDirty=false,
     *   因为脏属性过滤已由遍历阶段完成,避免二次判定把对象漏掉。
     */
    inline TVector<SObjectDomainSnapshotRecord> BuildObjectDomainSnapshotRecords(MObject* InRoot, EPropertyDomainFlags InDomain, bool bOnlyDirty = false) {
        TVector<SObjectDomainSnapshotRecord> Records;
        ForEachObjectInDomainSubtree(InRoot, InDomain, bOnlyDirty, [&](MObject* Current, const MString& CurrentPath) {
            TByteArray SnapshotData;
            if (!BuildObjectDomainSnapshot(Current, InDomain, SnapshotData, false)) {
                return;
            }

            MClass* CurrentClass = Current->GetClass();
            if (!CurrentClass) {
                return;
            }

            Records.push_back(SObjectDomainSnapshotRecord{
                InRoot ? InRoot->GetObjectId() : 0,
                Current->GetObjectId(),
                CurrentClass->GetId(),
                CurrentPath,
                CurrentClass->GetName(),
                std::move(SnapshotData),
            });
        });
        return Records;
    }

    /**
     * @brief 按 "A.B.C" 路径定位后代对象。
     * @param InRoot 查找起点;为空返回 nullptr
     * @param InPath 路径;空串表示返回 InRoot 自身
     * @return 命中的对象;任一段不存在时返回 nullptr
     *
     * @detail 每段只按名字做一次线性查找并取第一个同名子对象(名字不唯一时
     *   结果取决于 Children 顺序);不支持 '..'、绝对路径或数组下标语法。
     */
    inline MObject* ResolveObjectPath(MObject* InRoot, const MString& InPath) {
        if (!InRoot) {
            return nullptr;
        }

        if (InPath.empty()) {
            return InRoot;
        }

        MObject* Current      = InRoot;
        size_t   SegmentStart = 0;
        while (Current && SegmentStart < InPath.size()) {
            const size_t  DotIndex = InPath.find('.', SegmentStart);
            const MString Segment  = (DotIndex == MString::npos) ? InPath.substr(SegmentStart) : InPath.substr(SegmentStart, DotIndex - SegmentStart);

            MObject* Next = nullptr;
            for (MObject* Child : Current->GetChildren()) {
                if (Child && Child->GetName() == Segment) {
                    Next = Child;
                    break;
                }
            }

            Current = Next;
            if (DotIndex == MString::npos) {
                break;
            }
            SegmentStart = DotIndex + 1;
        }

        return Current;
    }

    /**
     * @brief 把记录列表回放到对象树上。
     * @param InRoot 回放目标树的根;为空即失败
     * @param Records BuildObjectDomainSnapshotRecords 的产物
     * @param InDomain 目标域(与抓取时一致,决定读回哪些属性)
     * @param OutError 失败原因,可空;格式为 "<原因>:<对象路径>"
     * @return true 全部回放成功
     *
     * @detail 逐条处理并"全部成功才返回 true":路径找不到、类名不匹配、
     *   负载提前耗尽或有多余字节都会立即失败并中止(此前的对象已被写入,
     *   本函数不做回滚)。记录的 RootObjectId/ObjectId/ClassId 仅作标识,
     *   定位只用 ObjectPath,校验只用 ClassName;成功写入后会调用
     *   ClearDirtyDomain 把该对象在本次域上的脏标记清掉。
     */
    inline bool ApplyObjectDomainSnapshotRecords(MObject* InRoot, const TVector<SObjectDomainSnapshotRecord>& Records, EPropertyDomainFlags InDomain, MString* OutError = nullptr) {
        if (!InRoot) {
            if (OutError) {
                *OutError = "missing_root";
            }
            return false;
        }

        for (const SObjectDomainSnapshotRecord& Record : Records) {
            MObject* TargetObject = ResolveObjectPath(InRoot, Record.ObjectPath);
            if (!TargetObject) {
                if (OutError) {
                    *OutError = "missing_object:" + Record.ObjectPath;
                }
                return false;
            }

            MClass* TargetClass = TargetObject->GetClass();
            if (!TargetClass) {
                if (OutError) {
                    *OutError = "missing_class:" + Record.ObjectPath;
                }
                return false;
            }

            if (!Record.ClassName.empty() && TargetClass->GetName() != Record.ClassName) {
                if (OutError) {
                    *OutError = "class_mismatch:" + Record.ObjectPath;
                }
                return false;
            }

            MReflectArchive Archive(Record.SnapshotData);
            if (!ReadClassDomainSnapshotRecursive(TargetClass, TargetObject, Archive, ToMask(InDomain))) {
                if (OutError) {
                    *OutError = "snapshot_read_overflow:" + Record.ObjectPath;
                }
                return false;
            }

            if (Archive.ReadPos != Record.SnapshotData.size()) {
                if (OutError) {
                    *OutError = "snapshot_trailing_bytes:" + Record.ObjectPath;
                }
                return false;
            }

            TargetObject->ClearDirtyDomain(InDomain);
        }

        return true;
    }
} // namespace MObjectDomainUtils
