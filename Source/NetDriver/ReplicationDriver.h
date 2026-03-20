#pragma once

#include "NetObject.h"
#include "Common/Socket/Socket.h"
#include "Messages/NetMessages.h"

// 连接通道
class MReplicationChannel
{
public:
    uint64 ConnectionId;
    TSet<uint64> RelevantActors;  // 该连接可见的Actor
    
    MReplicationChannel(uint64 InConnectionId) : ConnectionId(InConnectionId) {}
};

// 复制驱动 - 负责管理所有Actor的复制
class MReplicationDriver
{
private:
    // 所有复制的Actor
    TMap<uint64, MActor*> ReplicationMap;
    
    // 每个连接的相关Actor
    TMap<uint64, MReplicationChannel*> Channels;
    
    // 待发送的更新队列
    struct SPendingUpdate
    {
        uint64 ConnectionId;
        uint64 ActorId;
        TByteArray Data;
    };
    TQueue<SPendingUpdate> PendingUpdates;
    
    // 连接管理
    TMap<uint64, TSharedPtr<INetConnection>> Connections;
    
public:
    MReplicationDriver() = default;
    ~MReplicationDriver();

    // 复制驱动负责“谁需要同步给谁”，具体包格式在 cpp 内部收口。
    void RegisterActor(MActor* Actor);
    void UnregisterActor(uint64 ActorId);
    void AddConnection(uint64 ConnectionId, TSharedPtr<INetConnection> Connection);
    void RemoveConnection(uint64 ConnectionId);
    void AddRelevantActor(uint64 ConnectionId, uint64 ActorId);
    void RemoveRelevantActor(uint64 ConnectionId, uint64 ActorId);
    void Tick(float DeltaTime);
    void SendActorUpdate(uint64 ConnectionId, uint64 ActorId, const TByteArray& Data);
    void BroadcastActorCreate(MActor* Actor, uint64 ExcludeConnectionId = 0);
    void BroadcastActorDestroy(uint64 ActorId, uint64 ExcludeConnectionId = 0);
    
    // 获取连接数
    size_t GetConnectionCount() const { return Connections.size(); }
    
    // 获取Actor数
    size_t GetActorCount() const { return ReplicationMap.size(); }

private:
    TMap<uint64, TByteArray> LastSerializedSnapshots;

    void ProcessPendingUpdates();
};
