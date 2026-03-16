#include "ReplicationDriver.h"

namespace
{
TArray BuildActorUpdatePacket(uint64 ActorId, const TArray& Data)
{
    TArray Packet;
    const uint8 MsgType = static_cast<uint8>(EClientMessageType::MT_ActorUpdate);
    const uint32 DataSize = static_cast<uint32>(Data.size());

    Packet.push_back(MsgType);
    Packet.insert(Packet.end(), reinterpret_cast<const uint8*>(&ActorId), reinterpret_cast<const uint8*>(&ActorId) + sizeof(ActorId));
    Packet.insert(Packet.end(), reinterpret_cast<const uint8*>(&DataSize), reinterpret_cast<const uint8*>(&DataSize) + sizeof(DataSize));
    Packet.insert(Packet.end(), Data.begin(), Data.end());
    return Packet;
}

TArray BuildActorCreatePacket(MActor* Actor)
{
    TArray Packet;
    if (!Actor)
    {
        return Packet;
    }

    MMemoryArchive Ar;
    Actor->Serialize(Ar);

    const uint8 MsgType = static_cast<uint8>(EClientMessageType::MT_ActorCreate);
    const uint64 ActorId = Actor->GetObjectId();
    const uint32 DataSize = static_cast<uint32>(Ar.GetData().size());

    Packet.push_back(MsgType);
    Packet.insert(Packet.end(), reinterpret_cast<const uint8*>(&ActorId), reinterpret_cast<const uint8*>(&ActorId) + sizeof(ActorId));
    Packet.insert(Packet.end(), reinterpret_cast<const uint8*>(&DataSize), reinterpret_cast<const uint8*>(&DataSize) + sizeof(DataSize));
    Packet.insert(Packet.end(), Ar.GetData().begin(), Ar.GetData().end());
    return Packet;
}

TArray BuildActorDestroyPacket(uint64 ActorId)
{
    TArray Packet;
    const uint8 MsgType = static_cast<uint8>(EClientMessageType::MT_ActorDestroy);
    Packet.push_back(MsgType);
    Packet.insert(Packet.end(), reinterpret_cast<const uint8*>(&ActorId), reinterpret_cast<const uint8*>(&ActorId) + sizeof(ActorId));
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
        if (!Actor->NeedsNetUpdate())
        {
            continue;
        }

        MMemoryArchive Ar;
        Actor->GetReplicatedProperties(Ar);
        if (Ar.GetData().empty())
        {
            continue;
        }

        for (auto& [ConnectionId, Channel] : Channels)
        {
            if (Channel->RelevantActors.empty() || Channel->RelevantActors.count(ActorId) > 0)
            {
                auto ConnIt = Connections.find(ConnectionId);
                if (ConnIt != Connections.end() && ConnIt->second->IsConnected())
                {
                    SendActorUpdate(ConnectionId, ActorId, Ar.GetData());
                }
            }
        }

        Actor->ClearDirtyFlags();
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
