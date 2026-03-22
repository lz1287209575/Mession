#include "ReplicationDriver.h"
#include "Protocol/ServerMessages.h"
#include "Common/Net/ServerRpcRuntime.h"
#include "Common/Runtime/Reflect/Reflection.h"

namespace
{
bool BuildReflectedActorSnapshot(MActor* Actor, TByteArray& OutData)
{
    OutData.clear();
    if (!Actor)
    {
        return false;
    }

    auto* ReflectActor = dynamic_cast<MObject*>(Actor);
    if (!ReflectActor)
    {
        LOG_WARN("Replication requires reflected actor: actor_id=%llu",
                 static_cast<unsigned long long>(Actor->GetObjectId()));
        return false;
    }

    MClass* ActorClass = ReflectActor->GetClass();
    if (!ActorClass)
    {
        LOG_WARN("Replication missing class metadata: actor_id=%llu",
                 static_cast<unsigned long long>(Actor->GetObjectId()));
        return false;
    }

    MReflectArchive Ar;
    ActorClass->WriteSnapshot(Actor, Ar);
    OutData = Ar.Data;
    return true;
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

TByteArray BuildActorUpdatePacket(uint64 ActorId, const TByteArray& Data)
{
    TByteArray Packet;
    (void)BuildClientFunctionCallPacketByName("Client_OnActorUpdate", Packet, ActorId, Data);
    return Packet;
}

TByteArray BuildActorCreatePacket(MActor* Actor)
{
    TByteArray Packet;
    if (!Actor)
    {
        return Packet;
    }

    TByteArray SnapshotData;
    if (!BuildReflectedActorSnapshot(Actor, SnapshotData))
    {
        return Packet;
    }

    (void)BuildClientFunctionCallPacketByName(
        "Client_OnActorCreate",
        Packet,
        Actor->GetObjectId(),
        SnapshotData);
    return Packet;
}

TByteArray BuildActorDestroyPacket(uint64 ActorId)
{
    TByteArray Packet;
    (void)BuildClientFunctionCallPacketByName("Client_OnActorDestroy", Packet, ActorId);
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

void MReplicationDriver::RegisterActor(MActor* Actor)
{
    if (Actor && Actor->DoesActorReplicate())
    {
        ReplicationMap[Actor->GetObjectId()] = Actor;
        Actor->SetActorActive(true);
        TByteArray SnapshotData;
        if (BuildReflectedActorSnapshot(Actor, SnapshotData))
        {
            LastSerializedSnapshots[Actor->GetObjectId()] = SnapshotData;
        }
        LOG_DEBUG("Registered actor %llu for replication",
                  static_cast<unsigned long long>(Actor->GetObjectId()));
    }
}

void MReplicationDriver::UnregisterActor(uint64 ActorId)
{
    auto It = ReplicationMap.find(ActorId);
    if (It != ReplicationMap.end())
    {
        It->second->SetActorActive(false);
        ReplicationMap.erase(It);
    }
    LastSerializedSnapshots.erase(ActorId);
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

void MReplicationDriver::AddRelevantActor(uint64 ConnectionId, uint64 ActorId)
{
    auto It = Channels.find(ConnectionId);
    if (It != Channels.end())
    {
        It->second->RelevantActors.insert(ActorId);
    }
}

void MReplicationDriver::RemoveRelevantActor(uint64 ConnectionId, uint64 ActorId)
{
    auto It = Channels.find(ConnectionId);
    if (It != Channels.end())
    {
        It->second->RelevantActors.erase(ActorId);
    }
}

void MReplicationDriver::Tick(float)
{
    for (auto& [ActorId, Actor] : ReplicationMap)
    {
        if (!Actor || !Actor->IsReadyForNetUpdate())
        {
            continue;
        }

        MObject* ReflectActor = dynamic_cast<MObject*>(Actor);
        MClass* ActorClass = ReflectActor ? ReflectActor->GetClass() : nullptr;
        const bool bUseReplicationDirtyDomain =
            ReflectActor && ActorClass && ClassHasDomainProperties(ActorClass, EPropertyDomainFlags::Replication);

        if (bUseReplicationDirtyDomain &&
            !ReflectActor->HasAnyDirtyPropertyForDomain(EPropertyDomainFlags::Replication))
        {
            Actor->MarkNetUpdateSent();
            continue;
        }

        TByteArray SnapshotData;
        if (!BuildReflectedActorSnapshot(Actor, SnapshotData) || SnapshotData.empty())
        {
            continue;
        }

        TByteArray& LastSnapshot = LastSerializedSnapshots[ActorId];
        if (SnapshotData == LastSnapshot)
        {
            if (bUseReplicationDirtyDomain)
            {
                ReflectActor->ClearDirtyDomain(EPropertyDomainFlags::Replication);
            }
            Actor->MarkNetUpdateSent();
            continue;
        }

        for (auto& [ConnectionId, Channel] : Channels)
        {
            if (Channel->RelevantActors.empty() || Channel->RelevantActors.count(ActorId) > 0)
            {
                auto ConnIt = Connections.find(ConnectionId);
                if (ConnIt != Connections.end() && ConnIt->second->IsConnected())
                {
                    SendActorUpdate(ConnectionId, ActorId, SnapshotData);
                }
            }
        }

        LastSnapshot = SnapshotData;
        if (bUseReplicationDirtyDomain)
        {
            ReflectActor->ClearDirtyDomain(EPropertyDomainFlags::Replication);
        }
        Actor->MarkNetUpdateSent();
    }

    ProcessPendingUpdates();
}

void MReplicationDriver::SendActorUpdate(uint64 ConnectionId, uint64 ActorId, const TByteArray& Data)
{
    auto ConnIt = Connections.find(ConnectionId);
    if (ConnIt == Connections.end())
    {
        return;
    }

    const TByteArray Packet = BuildActorUpdatePacket(ActorId, Data);
    ConnIt->second->Send(Packet.data(), Packet.size());
}

void MReplicationDriver::BroadcastActorCreate(MActor* Actor, uint64 ExcludeConnectionId)
{
    if (!Actor)
    {
        return;
    }

    const TByteArray Packet = BuildActorCreatePacket(Actor);
    for (auto& [ConnectionId, Connection] : Connections)
    {
        if (ConnectionId != ExcludeConnectionId && Connection->IsConnected())
        {
            Connection->Send(Packet.data(), Packet.size());
        }
    }
}

void MReplicationDriver::BroadcastActorDestroy(uint64 ActorId, uint64 ExcludeConnectionId)
{
    const TByteArray Packet = BuildActorDestroyPacket(ActorId);
    for (auto& [ConnectionId, Connection] : Connections)
    {
        if (ConnectionId != ExcludeConnectionId && Connection->IsConnected())
        {
            Connection->Send(Packet.data(), Packet.size());
        }
    }

    UnregisterActor(ActorId);
}

void MReplicationDriver::ProcessPendingUpdates()
{
    // 实际发送待处理的消息
    // 这里可以优化为批量发送
}
