#include "Servers/World/Domain/PlayerSession.h"

MPlayerSession::MPlayerSession()
{
    Avatar = CreateDefaultSubObject<MPlayerAvatar>(this, "Avatar");
}

void MPlayerSession::InitializeForLogin(uint64 InPlayerId, uint64 InGatewayConnectionId, uint32 InSessionKey)
{
    PlayerId = InPlayerId;
    GatewayConnectionId = InGatewayConnectionId;
    SessionKey = InSessionKey;
    MarkPropertyDirty("PlayerId");
    MarkPropertyDirty("GatewayConnectionId");
    MarkPropertyDirty("SessionKey");

    if (Avatar)
    {
        Avatar->InitializeForPlayer(PlayerId, SceneId);
    }
}

void MPlayerSession::SetRoute(uint32 InSceneId, uint8 InTargetServerType)
{
    SceneId = InSceneId;
    TargetServerType = InTargetServerType;
    MarkPropertyDirty("SceneId");
    MarkPropertyDirty("TargetServerType");

    if (Avatar)
    {
        Avatar->SetCurrentSceneId(InSceneId);
    }
}

void MPlayerSession::ApplyPersistenceRecords(const TVector<FMgoPersistenceRecord>& Records)
{
    if (Records.empty())
    {
        return;
    }

    for (const FMgoPersistenceRecord& Record : Records)
    {
        MObject* TargetObject = ResolvePersistenceObjectByPath(Record.ObjectPath);
        if (!TargetObject)
        {
            continue;
        }

        MClass* TargetClass = TargetObject->GetClass();
        if (!TargetClass || (!Record.ClassName.empty() && TargetClass->GetName() != Record.ClassName))
        {
            continue;
        }

        TargetClass->ReadSnapshotByDomain(TargetObject, Record.SnapshotData, ToMask(EPropertyDomainFlags::Persistence));
        TargetObject->ClearDirtyDomain(EPropertyDomainFlags::Persistence);
    }
}

TVector<FMgoPersistenceRecord> MPlayerSession::BuildPersistenceRecords() const
{
    TVector<FMgoPersistenceRecord> Records;
    BuildPersistenceSnapshotRecords(this, "", Records);
    return Records;
}

MPlayerAvatar* MPlayerSession::GetAvatar() const
{
    return Avatar;
}

void MPlayerSession::VisitReferencedObjects(const TFunction<void(MObject*)>& Visitor) const
{
    MObject::VisitReferencedObjects(Visitor);
    if (Visitor)
    {
        Visitor(Avatar);
    }
}

bool MPlayerSession::ClassHasPersistenceProperties(const MClass* InClass)
{
    if (!InClass)
    {
        return false;
    }

    for (const MProperty* Prop : InClass->GetProperties())
    {
        if (Prop && Prop->HasAnyDomains(EPropertyDomainFlags::Persistence))
        {
            return true;
        }
    }

    return false;
}

void MPlayerSession::BuildPersistenceSnapshotRecords(
    const MObject* InObject,
    const MString& InPath,
    TVector<FMgoPersistenceRecord>& OutRecords)
{
    if (!InObject)
    {
        return;
    }

    MClass* LocalClass = InObject->GetClass();
    if (LocalClass && ClassHasPersistenceProperties(LocalClass))
    {
        MReflectArchive SnapshotArchive;
        LocalClass->WriteSnapshotByDomain(const_cast<MObject*>(InObject), SnapshotArchive, ToMask(EPropertyDomainFlags::Persistence));
        OutRecords.push_back(FMgoPersistenceRecord{
            InPath,
            LocalClass->GetName(),
            std::move(SnapshotArchive.Data),
        });
    }

    for (MObject* Child : InObject->GetChildren())
    {
        if (!Child)
        {
            continue;
        }

        const MString ChildPath = InPath.empty() ? Child->GetName() : (InPath + "." + Child->GetName());
        BuildPersistenceSnapshotRecords(Child, ChildPath, OutRecords);
    }
}

MObject* MPlayerSession::ResolvePersistenceObjectByPath(const MString& InPath)
{
    if (InPath.empty())
    {
        return this;
    }

    MObject* Current = this;
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
