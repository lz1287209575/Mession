#pragma once

#include "Common/Runtime/Reflect/Reflection.h"

struct SObjectDomainSnapshotRecord
{
    uint64 RootObjectId = 0;
    uint64 ObjectId = 0;
    uint16 ClassId = 0;
    MString ObjectPath;
    MString ClassName;
    TByteArray SnapshotData;
};

namespace MObjectDomainUtils
{
inline void WriteClassDomainSnapshotRecursive(
    const MClass* InClass,
    void* InObject,
    MReflectArchive& Archive,
    uint64 InDomainMask)
{
    if (!InClass || !InObject || InDomainMask == 0)
    {
        return;
    }

    WriteClassDomainSnapshotRecursive(InClass->GetParentClass(), InObject, Archive, InDomainMask);
    for (const MProperty* Prop : InClass->GetProperties())
    {
        if (!Prop || (Prop->DomainFlags & InDomainMask) == 0)
        {
            continue;
        }

        Prop->WriteValue(InObject, Archive);
    }
}

inline bool ReadClassDomainSnapshotRecursive(
    const MClass* InClass,
    void* InObject,
    MReflectArchive& Archive,
    uint64 InDomainMask)
{
    if (!InClass || !InObject || InDomainMask == 0)
    {
        return true;
    }

    if (!ReadClassDomainSnapshotRecursive(InClass->GetParentClass(), InObject, Archive, InDomainMask))
    {
        return false;
    }

    for (const MProperty* Prop : InClass->GetProperties())
    {
        if (!Prop || (Prop->DomainFlags & InDomainMask) == 0)
        {
            continue;
        }

        Prop->WriteValue(InObject, Archive);
        if (Archive.bReadOverflow)
        {
            return false;
        }
    }

    return true;
}

inline bool ClassHasDomainProperties(const MClass* InClass, EPropertyDomainFlags InDomain)
{
    for (const MClass* Current = InClass; Current; Current = Current->GetParentClass())
    {
        for (const MProperty* Prop : Current->GetProperties())
        {
            if (Prop && Prop->HasAnyDomains(InDomain))
            {
                return true;
            }
        }
    }

    return false;
}

inline bool ShouldCaptureObject(MObject* InObject, EPropertyDomainFlags InDomain, bool bOnlyDirty)
{
    if (!InObject)
    {
        return false;
    }

    MClass* ObjectClass = InObject->GetClass();
    if (!ObjectClass || !ClassHasDomainProperties(ObjectClass, InDomain))
    {
        return false;
    }

    return !bOnlyDirty || InObject->HasAnyDirtyPropertyForDomain(InDomain);
}

inline bool BuildObjectDomainSnapshot(
    MObject* InObject,
    EPropertyDomainFlags InDomain,
    TByteArray& OutData,
    bool bOnlyDirty = false)
{
    OutData.clear();
    if (!ShouldCaptureObject(InObject, InDomain, bOnlyDirty))
    {
        return false;
    }

    MReflectArchive Archive;
    WriteClassDomainSnapshotRecursive(InObject->GetClass(), InObject, Archive, ToMask(InDomain));
    OutData = std::move(Archive.Data);
    return true;
}

template<typename TVisitor>
inline void ForEachObjectInDomainSubtree(
    MObject* InRoot,
    EPropertyDomainFlags InDomain,
    bool bOnlyDirty,
    TVisitor&& Visitor)
{
    if (!InRoot)
    {
        return;
    }

    TFunction<void(MObject*, const MString&)> VisitSubtree;
    VisitSubtree = [&](MObject* Current, const MString& CurrentPath)
    {
        if (!Current)
        {
            return;
        }

        if (ShouldCaptureObject(Current, InDomain, bOnlyDirty))
        {
            Visitor(Current, CurrentPath);
        }

        for (MObject* Child : Current->GetChildren())
        {
            if (!Child)
            {
                continue;
            }

            const MString ChildPath = CurrentPath.empty()
                ? Child->GetName()
                : (CurrentPath + "." + Child->GetName());
            VisitSubtree(Child, ChildPath);
        }
    };

    VisitSubtree(InRoot, "");
}

inline TVector<SObjectDomainSnapshotRecord> BuildObjectDomainSnapshotRecords(
    MObject* InRoot,
    EPropertyDomainFlags InDomain,
    bool bOnlyDirty = false)
{
    TVector<SObjectDomainSnapshotRecord> Records;
    ForEachObjectInDomainSubtree(
        InRoot,
        InDomain,
        bOnlyDirty,
        [&](MObject* Current, const MString& CurrentPath)
        {
            TByteArray SnapshotData;
            if (!BuildObjectDomainSnapshot(Current, InDomain, SnapshotData, false))
            {
                return;
            }

            MClass* CurrentClass = Current->GetClass();
            if (!CurrentClass)
            {
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

inline MObject* ResolveObjectPath(MObject* InRoot, const MString& InPath)
{
    if (!InRoot)
    {
        return nullptr;
    }

    if (InPath.empty())
    {
        return InRoot;
    }

    MObject* Current = InRoot;
    size_t SegmentStart = 0;
    while (Current && SegmentStart < InPath.size())
    {
        const size_t DotIndex = InPath.find('.', SegmentStart);
        const MString Segment = (DotIndex == MString::npos)
            ? InPath.substr(SegmentStart)
            : InPath.substr(SegmentStart, DotIndex - SegmentStart);

        MObject* Next = nullptr;
        for (MObject* Child : Current->GetChildren())
        {
            if (Child && Child->GetName() == Segment)
            {
                Next = Child;
                break;
            }
        }

        Current = Next;
        if (DotIndex == MString::npos)
        {
            break;
        }
        SegmentStart = DotIndex + 1;
    }

    return Current;
}

inline bool ApplyObjectDomainSnapshotRecords(
    MObject* InRoot,
    const TVector<SObjectDomainSnapshotRecord>& Records,
    EPropertyDomainFlags InDomain,
    MString* OutError = nullptr)
{
    if (!InRoot)
    {
        if (OutError)
        {
            *OutError = "missing_root";
        }
        return false;
    }

    for (const SObjectDomainSnapshotRecord& Record : Records)
    {
        MObject* TargetObject = ResolveObjectPath(InRoot, Record.ObjectPath);
        if (!TargetObject)
        {
            if (OutError)
            {
                *OutError = "missing_object:" + Record.ObjectPath;
            }
            return false;
        }

        MClass* TargetClass = TargetObject->GetClass();
        if (!TargetClass)
        {
            if (OutError)
            {
                *OutError = "missing_class:" + Record.ObjectPath;
            }
            return false;
        }

        if (!Record.ClassName.empty() && TargetClass->GetName() != Record.ClassName)
        {
            if (OutError)
            {
                *OutError = "class_mismatch:" + Record.ObjectPath;
            }
            return false;
        }

        MReflectArchive Archive(Record.SnapshotData);
        if (!ReadClassDomainSnapshotRecursive(TargetClass, TargetObject, Archive, ToMask(InDomain)))
        {
            if (OutError)
            {
                *OutError = "snapshot_read_overflow:" + Record.ObjectPath;
            }
            return false;
        }

        if (Archive.ReadPos != Record.SnapshotData.size())
        {
            if (OutError)
            {
                *OutError = "snapshot_trailing_bytes:" + Record.ObjectPath;
            }
            return false;
        }

        TargetObject->ClearDirtyDomain(InDomain);
    }

    return true;
}
} // namespace MObjectDomainUtils
