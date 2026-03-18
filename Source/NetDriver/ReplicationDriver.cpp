#include "ReplicationDriver.h"
#include "Common/ServerMessages.h"
#include "Common/ServerRpcRuntime.h"

namespace
{
bool BuildReflectedActorSnapshot(MActor* Actor, TArray& OutData)
{
    OutData.clear();
    if (!Actor)
    {
        return false;
    }

    auto* ReflectActor = dynamic_cast<MReflectObject*>(Actor);
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

TArray BuildActorUpdatePacket(uint64 ActorId, const TArray& Data)
{
    TArray Packet;
    BuildClientFunctionCallPacketForPayload(
        MClientDownlink::OnActorUpdate,
        SClientActorUpdatePayload{ActorId, Data},
        Packet);
    return Packet;
}

TArray BuildActorCreatePacket(MActor* Actor)
{
    TArray Packet;
    if (!Actor)
    {
        return Packet;
    }

    TArray SnapshotData;
    if (!BuildReflectedActorSnapshot(Actor, SnapshotData))
    {
        return Packet;
    }

    BuildClientFunctionCallPacketForPayload(
        MClientDownlink::OnActorCreate,
        SClientActorCreatePayload{Actor->GetObjectId(), SnapshotData},
        Packet);
    return Packet;
}

TArray BuildActorDestroyPacket(uint64 ActorId)
{
    TArray Packet;
    BuildClientFunctionCallPacketForPayload(
        MClientDownlink::OnActorDestroy,
        SClientActorDestroyPayload{ActorId},
        Packet);
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
        TArray SnapshotData;
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

        TArray SnapshotData;
        if (!BuildReflectedActorSnapshot(Actor, SnapshotData) || SnapshotData.empty())
        {
            continue;
        }

        TArray& LastSnapshot = LastSerializedSnapshots[ActorId];
        if (SnapshotData == LastSnapshot)
        {
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
        Actor->MarkNetUpdateSent();
    }

    ProcessPendingUpdates();
}

void MReplicationDriver::SendActorUpdate(uint64 ConnectionId, uint64 ActorId, const TArray& Data)
{
    auto ConnIt = Connections.find(ConnectionId);
    if (ConnIt == Connections.end())
    {
        return;
    }

    const TArray Packet = BuildActorUpdatePacket(ActorId, Data);
    ConnIt->second->Send(Packet.data(), Packet.size());
}

void MReplicationDriver::BroadcastActorCreate(MActor* Actor, uint64 ExcludeConnectionId)
{
    if (!Actor)
    {
        return;
    }

    const TArray Packet = BuildActorCreatePacket(Actor);
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
    const TArray Packet = BuildActorDestroyPacket(ActorId);
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
