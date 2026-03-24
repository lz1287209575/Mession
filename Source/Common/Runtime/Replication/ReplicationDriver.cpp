#include "ReplicationDriver.h"
#include "Protocol/ServerMessages.h"
#include "Common/Net/Rpc/RpcClientCall.h"
#include "Common/Runtime/Reflect/Reflection.h"

namespace
{
bool ClassHasDomainProperties(const MClass* InClass, EPropertyDomainFlags InDomain);

bool BuildReflectedObjectSnapshot(MObject* Object, TByteArray& OutData)
{
    OutData.clear();
    if (!Object)
    {
        return false;
    }

    MClass* ObjectClass = Object->GetClass();
    if (!ObjectClass)
    {
        LOG_WARN("Replication missing class metadata: object_id=%llu",
                 static_cast<unsigned long long>(Object->GetObjectId()));
        return false;
    }

    MReflectArchive Ar;
    ObjectClass->WriteSnapshot(Object, Ar);
    OutData = Ar.Data;
    return true;
}

bool SupportsObjectReplication(MObject* Object)
{
    if (!Object)
    {
        return false;
    }

    if (MActor* Actor = dynamic_cast<MActor*>(Object))
    {
        if (Actor->DoesActorReplicate())
        {
            return true;
        }
    }

    MClass* ObjectClass = Object->GetClass();
    return ClassHasDomainProperties(ObjectClass, EPropertyDomainFlags::Replication);
}

bool ClassHasDomainProperties(const MClass* InClass, EPropertyDomainFlags InDomain)
{
    if (!InClass)
    {
        return false;
    }

    for (const MProperty* Prop : InClass->GetProperties())
    {
        if (Prop && Prop->HasAnyDomains(InDomain))
        {
            return true;
        }
    }
    return false;
}

TByteArray BuildObjectUpdatePacket(uint64 ObjectId, const TByteArray& Data)
{
    TByteArray Packet;
    (void)BuildClientFunctionCallPacketByName("Client_OnObjectUpdate", Packet, ObjectId, Data);
    return Packet;
}

TByteArray BuildObjectCreatePacket(MObject* Object)
{
    TByteArray Packet;
    if (!Object)
    {
        return Packet;
    }

    TByteArray SnapshotData;
    if (!BuildReflectedObjectSnapshot(Object, SnapshotData))
    {
        return Packet;
    }

    (void)BuildClientFunctionCallPacketByName(
        "Client_OnObjectCreate",
        Packet,
        Object->GetObjectId(),
        SnapshotData);
    return Packet;
}

TByteArray BuildObjectDestroyPacket(uint64 ObjectId)
{
    TByteArray Packet;
    (void)BuildClientFunctionCallPacketByName("Client_OnObjectDestroy", Packet, ObjectId);
    return Packet;
}
}

MReplicationDriver::~MReplicationDriver()
{
    for (auto& [Id, Channel] : Channels)
    {
        delete Channel;
    }
    Channels.clear();
}

void MReplicationDriver::RegisterObject(MObject* Object)
{
    if (SupportsObjectReplication(Object))
    {
        ReplicationMap[Object->GetObjectId()] = Object;
        if (MActor* Actor = dynamic_cast<MActor*>(Object))
        {
            Actor->SetActorActive(true);
        }
        TByteArray SnapshotData;
        if (BuildReflectedObjectSnapshot(Object, SnapshotData))
        {
            LastSerializedSnapshots[Object->GetObjectId()] = SnapshotData;
        }
        LOG_DEBUG("Registered object %llu for replication",
                  static_cast<unsigned long long>(Object->GetObjectId()));
    }
}

void MReplicationDriver::UnregisterObject(uint64 ObjectId)
{
    auto It = ReplicationMap.find(ObjectId);
    if (It != ReplicationMap.end())
    {
        if (MActor* Actor = dynamic_cast<MActor*>(It->second))
        {
            Actor->SetActorActive(false);
        }
        ReplicationMap.erase(It);
    }
    LastSerializedSnapshots.erase(ObjectId);
}

void MReplicationDriver::RegisterActor(MActor* Actor)
{
    RegisterObject(Actor);
}

void MReplicationDriver::UnregisterActor(uint64 ActorId)
{
    UnregisterObject(ActorId);
}

void MReplicationDriver::AddConnection(uint64 ConnectionId, TSharedPtr<INetConnection> Connection)
{
    Connections[ConnectionId] = Connection;
    Channels[ConnectionId] = new MReplicationChannel(ConnectionId);
    LOG_DEBUG("Added connection %llu to replication", static_cast<unsigned long long>(ConnectionId));
}

void MReplicationDriver::RemoveConnection(uint64 ConnectionId)
{
    Connections.erase(ConnectionId);

    auto It = Channels.find(ConnectionId);
    if (It != Channels.end())
    {
        delete It->second;
        Channels.erase(It);
    }
}

void MReplicationDriver::AddRelevantObject(uint64 ConnectionId, uint64 ObjectId)
{
    auto It = Channels.find(ConnectionId);
    if (It != Channels.end())
    {
        It->second->RelevantObjects.insert(ObjectId);
    }
}

void MReplicationDriver::RemoveRelevantObject(uint64 ConnectionId, uint64 ObjectId)
{
    auto It = Channels.find(ConnectionId);
    if (It != Channels.end())
    {
        It->second->RelevantObjects.erase(ObjectId);
    }
}

void MReplicationDriver::AddRelevantActor(uint64 ConnectionId, uint64 ActorId)
{
    AddRelevantObject(ConnectionId, ActorId);
}

void MReplicationDriver::RemoveRelevantActor(uint64 ConnectionId, uint64 ActorId)
{
    RemoveRelevantObject(ConnectionId, ActorId);
}

void MReplicationDriver::Tick(float DeltaTime)
{
    for (auto& [ObjectId, Object] : ReplicationMap)
    {
        if (!Object)
        {
            continue;
        }

        MActor* Actor = dynamic_cast<MActor*>(Object);
        if (Actor)
        {
            Actor->Tick(DeltaTime);
            if (Actor->DoesActorReplicate() && !Actor->IsReadyForNetUpdate())
            {
                continue;
            }
        }

        MClass* ObjectClass = Object->GetClass();
        const bool bUseReplicationDirtyDomain =
            Object && ObjectClass && ClassHasDomainProperties(ObjectClass, EPropertyDomainFlags::Replication);

        if (bUseReplicationDirtyDomain &&
            !Object->HasAnyDirtyPropertyForDomain(EPropertyDomainFlags::Replication))
        {
            if (Actor)
            {
                Actor->MarkNetUpdateSent();
            }
            continue;
        }

        TByteArray SnapshotData;
        if (!BuildReflectedObjectSnapshot(Object, SnapshotData) || SnapshotData.empty())
        {
            continue;
        }

        TByteArray& LastSnapshot = LastSerializedSnapshots[ObjectId];
        if (SnapshotData == LastSnapshot)
        {
            if (bUseReplicationDirtyDomain)
            {
                Object->ClearDirtyDomain(EPropertyDomainFlags::Replication);
            }
            if (Actor)
            {
                Actor->MarkNetUpdateSent();
            }
            continue;
        }

        for (auto& [ConnectionId, Channel] : Channels)
        {
            if (Channel->RelevantObjects.empty() || Channel->RelevantObjects.count(ObjectId) > 0)
            {
                auto ConnIt = Connections.find(ConnectionId);
                if (ConnIt != Connections.end() && ConnIt->second->IsConnected())
                {
                    SendObjectUpdate(ConnectionId, ObjectId, SnapshotData);
                }
            }
        }

        LastSnapshot = SnapshotData;
        if (bUseReplicationDirtyDomain)
        {
            Object->ClearDirtyDomain(EPropertyDomainFlags::Replication);
        }
        if (Actor)
        {
            Actor->MarkNetUpdateSent();
        }
    }

    ProcessPendingUpdates();
}

void MReplicationDriver::SendObjectUpdate(uint64 ConnectionId, uint64 ObjectId, const TByteArray& Data)
{
    auto ConnIt = Connections.find(ConnectionId);
    if (ConnIt == Connections.end())
    {
        return;
    }

    const TByteArray Packet = BuildObjectUpdatePacket(ObjectId, Data);
    ConnIt->second->Send(Packet.data(), Packet.size());
}

void MReplicationDriver::BroadcastObjectCreate(MObject* Object, uint64 ExcludeConnectionId)
{
    if (!Object)
    {
        return;
    }

    const TByteArray Packet = BuildObjectCreatePacket(Object);
    for (auto& [ConnectionId, Connection] : Connections)
    {
        if (ConnectionId != ExcludeConnectionId && Connection->IsConnected())
        {
            Connection->Send(Packet.data(), Packet.size());
        }
    }
}

void MReplicationDriver::BroadcastObjectDestroy(uint64 ObjectId, uint64 ExcludeConnectionId)
{
    const TByteArray Packet = BuildObjectDestroyPacket(ObjectId);
    for (auto& [ConnectionId, Connection] : Connections)
    {
        if (ConnectionId != ExcludeConnectionId && Connection->IsConnected())
        {
            Connection->Send(Packet.data(), Packet.size());
        }
    }

    UnregisterObject(ObjectId);
}

void MReplicationDriver::SendActorUpdate(uint64 ConnectionId, uint64 ActorId, const TByteArray& Data)
{
    SendObjectUpdate(ConnectionId, ActorId, Data);
}

void MReplicationDriver::BroadcastActorCreate(MActor* Actor, uint64 ExcludeConnectionId)
{
    BroadcastObjectCreate(Actor, ExcludeConnectionId);
}

void MReplicationDriver::BroadcastActorDestroy(uint64 ActorId, uint64 ExcludeConnectionId)
{
    BroadcastObjectDestroy(ActorId, ExcludeConnectionId);
}

void MReplicationDriver::ProcessPendingUpdates()
{
    // 实际发送待处理的消息
    // 这里可以优化为批量发送
}
