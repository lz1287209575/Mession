#pragma once

#include "Common/Runtime/Object/ObjectDomainUtils.h"

struct SPersistenceRecord
{
    uint64 RootObjectId = 0;
    uint64 ObjectId = 0;
    uint16 ClassId = 0;
    uint32 OwnerServerId = 0;
    uint64 RequestId = 0;
    uint64 Version = 0;
    MString ObjectPath;
    MString ClassName;
    TByteArray SnapshotData;
};

class IPersistenceSink
{
public:
    virtual ~IPersistenceSink() = default;
    virtual bool Persist(const SPersistenceRecord& InRecord) = 0;
};

class MNoopPersistenceSink : public IPersistenceSink
{
public:
    bool Persist(const SPersistenceRecord&) override
    {
        return true;
    }
};

class MPersistenceSubsystem
{
public:
    MPersistenceSubsystem()
        : Sink(std::make_unique<MNoopPersistenceSink>())
    {
    }

    void SetSink(TUniquePtr<IPersistenceSink> InSink)
    {
        if (InSink)
        {
            Sink = std::move(InSink);
        }
    }

    void SetOwnerServerId(uint32 InOwnerServerId)
    {
        OwnerServerId = InOwnerServerId;
    }

    TVector<SPersistenceRecord> BuildRecordsForRoot(MObject* InRoot, bool bOnlyDirty = false)
    {
        TVector<SPersistenceRecord> Records;
        if (!InRoot)
        {
            return Records;
        }

        const TVector<SObjectDomainSnapshotRecord> SnapshotRecords =
            MObjectDomainUtils::BuildObjectDomainSnapshotRecords(
                InRoot,
                EPropertyDomainFlags::Persistence,
                bOnlyDirty);

        Records.reserve(SnapshotRecords.size());
        for (const SObjectDomainSnapshotRecord& SnapshotRecord : SnapshotRecords)
        {
            SPersistenceRecord Record;
            Record.RootObjectId = SnapshotRecord.RootObjectId;
            Record.ObjectId = SnapshotRecord.ObjectId;
            Record.ClassId = SnapshotRecord.ClassId;
            Record.OwnerServerId = OwnerServerId;
            Record.ObjectPath = SnapshotRecord.ObjectPath;
            Record.ClassName = SnapshotRecord.ClassName;
            Record.SnapshotData = SnapshotRecord.SnapshotData;
            Records.push_back(std::move(Record));
        }

        return Records;
    }

    bool EnqueueRootIfDirty(MObject* InRoot)
    {
        bool bEnqueuedAny = false;
        for (SPersistenceRecord& Record : BuildRecordsForRoot(InRoot, true))
        {
            uint64& ObjectVersion = ObjectVersions[Record.ObjectId];
            ++ObjectVersion;
            Record.Version = ObjectVersion;
            Record.RequestId = NextRequestId++;
            if (Record.RequestId == 0)
            {
                Record.RequestId = NextRequestId++;
            }

            auto ExistingIt = PendingRecords.find(Record.ObjectId);
            if (ExistingIt == PendingRecords.end())
            {
                PendingRecords[Record.ObjectId] = Record;
                PendingOrder.push_back(Record.ObjectId);
            }
            else
            {
                ExistingIt->second = Record;
                ++MergedCount;
            }

            ++EnqueuedCount;
            bEnqueuedAny = true;

            if (MObject* PendingObject = MObject::FindObject(Record.ObjectId))
            {
                PendingObject->ClearDirtyDomain(EPropertyDomainFlags::Persistence);
            }
        }

        return bEnqueuedAny;
    }

    uint32 Flush(uint32 InMaxRecords = 64)
    {
        if (!Sink)
        {
            return 0;
        }

        uint32 Flushed = 0;
        while (!PendingOrder.empty() && Flushed < InMaxRecords)
        {
            const uint64 ObjectId = PendingOrder.front();
            auto RecordIt = PendingRecords.find(ObjectId);
            if (RecordIt == PendingRecords.end())
            {
                PendingOrder.pop_front();
                continue;
            }

            if (!Sink->Persist(RecordIt->second))
            {
                ++RetryBlockedCount;
                break;
            }

            PendingRecords.erase(RecordIt);
            PendingOrder.pop_front();
            ++Flushed;
            ++FlushedCount;
        }
        return Flushed;
    }

    uint64 GetPendingCount() const
    {
        return static_cast<uint64>(PendingOrder.size());
    }

    uint64 GetEnqueuedCount() const
    {
        return EnqueuedCount;
    }

    uint64 GetFlushedCount() const
    {
        return FlushedCount;
    }

    uint64 GetMergedCount() const
    {
        return MergedCount;
    }

    uint64 GetRetryBlockedCount() const
    {
        return RetryBlockedCount;
    }

private:
    TMap<uint64, SPersistenceRecord> PendingRecords;
    TDeque<uint64> PendingOrder;
    TMap<uint64, uint64> ObjectVersions;
    TUniquePtr<IPersistenceSink> Sink;
    uint32 OwnerServerId = 0;
    uint64 NextRequestId = 1;
    uint64 EnqueuedCount = 0;
    uint64 FlushedCount = 0;
    uint64 MergedCount = 0;
    uint64 RetryBlockedCount = 0;
};
