#pragma once

#include "Common/Runtime/Reflect/Reflection.h"

struct SPersistenceRecord
{
    uint64 ObjectId = 0;
    uint16 ClassId = 0;
    uint32 OwnerServerId = 0;
    uint64 RequestId = 0;
    uint64 Version = 0;
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

    bool EnqueueIfDirty(MObject* InObject, MClass* InClass = nullptr)
    {
        if (!InObject)
        {
            return false;
        }

        MClass* LocalClass = InClass ? InClass : InObject->GetClass();
        if (!LocalClass)
        {
            return false;
        }

        if (!InObject->HasAnyDirtyPropertyForDomain(EPropertyDomainFlags::Persistence))
        {
            return false;
        }

        MReflectArchive Ar;
        LocalClass->WriteSnapshotByDomain(InObject, Ar, ToMask(EPropertyDomainFlags::Persistence));

        SPersistenceRecord Record;
        Record.ObjectId = InObject->GetId();
        Record.ClassId = LocalClass->GetId();
        Record.OwnerServerId = OwnerServerId;
        uint64& ObjectVersion = ObjectVersions[Record.ObjectId];
        ++ObjectVersion;
        Record.Version = ObjectVersion;
        Record.RequestId = NextRequestId++;
        if (Record.RequestId == 0)
        {
            Record.RequestId = NextRequestId++;
        }
        Record.ClassName = LocalClass->GetName();
        Record.SnapshotData = std::move(Ar.Data);
        auto ExistingIt = PendingRecords.find(Record.ObjectId);
        if (ExistingIt == PendingRecords.end())
        {
            PendingRecords[Record.ObjectId] = std::move(Record);
            PendingOrder.push_back(InObject->GetId());
        }
        else
        {
            ExistingIt->second = std::move(Record);
            ++MergedCount;
        }
        ++EnqueuedCount;

        InObject->ClearDirtyDomain(EPropertyDomainFlags::Persistence);
        return true;
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
