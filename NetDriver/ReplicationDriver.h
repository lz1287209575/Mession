#pragma once

#include "NetObject.h"
#include "../Core/Socket.h"

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
        TArray Data;
    };
    TQueue<SPendingUpdate> PendingUpdates;
    
    // 连接管理
    TMap<uint64, TSharedPtr<MTcpConnection>> Connections;
    
public:
    MReplicationDriver() {}
    ~MReplicationDriver();
    
    // 注册Actor
    void RegisterActor(MActor* Actor)
    {
        if (Actor && Actor->DoesActorReplicate())
        {
            ReplicationMap[Actor->GetObjectId()] = Actor;
            Actor->SetActorActive(true);
            LOG_DEBUG("Registered actor %llu for replication", 
                      (unsigned long long)Actor->GetObjectId());
        }
    }
    
    // 注销Actor
    void UnregisterActor(uint64 ActorId)
    {
        auto It = ReplicationMap.find(ActorId);
        if (It != ReplicationMap.end())
        {
            It->second->SetActorActive(false);
            ReplicationMap.erase(It);
        }
    }
    
    // 添加连接到频道
    void AddConnection(uint64 ConnectionId, TSharedPtr<MTcpConnection> Connection)
    {
        Connections[ConnectionId] = Connection;
        Channels[ConnectionId] = new MReplicationChannel(ConnectionId);
        LOG_DEBUG("Added connection %llu to replication", (unsigned long long)ConnectionId);
    }
    
    // 移除连接
    void RemoveConnection(uint64 ConnectionId)
    {
        Connections.erase(ConnectionId);
        
        auto It = Channels.find(ConnectionId);
        if (It != Channels.end())
        {
            delete It->second;
            Channels.erase(It);
        }
    }
    
    // 设置连接可见的Actor
    void AddRelevantActor(uint64 ConnectionId, uint64 ActorId)
    {
        auto It = Channels.find(ConnectionId);
        if (It != Channels.end())
        {
            It->second->RelevantActors.insert(ActorId);
        }
    }
    
    // 移除连接可见的Actor
    void RemoveRelevantActor(uint64 ConnectionId, uint64 ActorId)
    {
        auto It = Channels.find(ConnectionId);
        if (It != Channels.end())
        {
            It->second->RelevantActors.erase(ActorId);
        }
    }
    
    // Tick - 处理复制
    void Tick(float /*DeltaTime*/)
    {
        // 1. 检查所有Actor是否需要更新
        for (auto& [ActorId, Actor] : ReplicationMap)
        {
            if (!Actor->NeedsNetUpdate())
                continue;
            
            // 序列化更新的属性
            MMemoryArchive Ar;
            Actor->GetReplicatedProperties(Ar);
            
            if (Ar.GetData().empty())
                continue;
            
            // 发送给所有相关连接
            for (auto& [ConnectionId, Channel] : Channels)
            {
                // 检查该连接是否应该看到这个Actor
                if (Channel->RelevantActors.empty() || 
                    Channel->RelevantActors.count(ActorId) > 0)
                {
                    // 发送到客户端
                    auto ConnIt = Connections.find(ConnectionId);
                    if (ConnIt != Connections.end() && ConnIt->second->IsConnected())
                    {
                        SendActorUpdate(ConnectionId, ActorId, Ar.GetData());
                    }
                }
            }
            
            // 清除脏标记
            Actor->ClearDirtyFlags();
        }
        
        // 2. 处理待发送队列
        ProcessPendingUpdates();
    }
    
    // 直接发送Actor更新
    void SendActorUpdate(uint64 ConnectionId, uint64 ActorId, const TArray& Data)
    {
        // 消息格式: [MsgType(1)][ActorId(8)][DataSize(4)][Data...]
        TArray Packet;
        uint8 MsgType = 8; // ActorUpdate
        uint64 ActorIdBE = ActorId;
        uint32 DataSize = (uint32)Data.size();
        
        Packet.push_back(MsgType);
        // 添加Actor ID
        Packet.insert(Packet.end(), (uint8*)&ActorIdBE, (uint8*)&ActorIdBE + sizeof(ActorIdBE));
        // 添加数据大小
        Packet.insert(Packet.end(), (uint8*)&DataSize, (uint8*)&DataSize + sizeof(DataSize));
        // 添加数据
        Packet.insert(Packet.end(), Data.begin(), Data.end());
        
        auto ConnIt = Connections.find(ConnectionId);
        if (ConnIt != Connections.end())
        {
            ConnIt->second->Send(Packet.data(), Packet.size());
        }
    }
    
    // 广播Actor创建
    void BroadcastActorCreate(MActor* Actor, uint64 ExcludeConnectionId = 0)
    {
        if (!Actor)
            return;
        
        MMemoryArchive Ar;
        Actor->Serialize(Ar);
        
        // 创建消息
        TArray Packet;
        uint8 MsgType = 3; // ActorCreate
        uint64 ActorId = Actor->GetObjectId();
        uint32 DataSize = (uint32)Ar.GetData().size();
        
        Packet.push_back(MsgType);
        Packet.insert(Packet.end(), (uint8*)&ActorId, (uint8*)&ActorId + sizeof(ActorId));
        Packet.insert(Packet.end(), (uint8*)&DataSize, (uint8*)&DataSize + sizeof(DataSize));
        Packet.insert(Packet.end(), Ar.GetData().begin(), Ar.GetData().end());
        
        // 发送给所有连接
        for (auto& [ConnectionId, Connection] : Connections)
        {
            if (ConnectionId != ExcludeConnectionId && Connection->IsConnected())
            {
                Connection->Send(Packet.data(), Packet.size());
            }
        }
    }
    
    // 广播Actor销毁
    void BroadcastActorDestroy(uint64 ActorId, uint64 ExcludeConnectionId = 0)
    {
        TArray Packet;
        uint8 MsgType = 4; // ActorDestroy
        Packet.push_back(MsgType);
        Packet.insert(Packet.end(), (uint8*)&ActorId, (uint8*)&ActorId + sizeof(ActorId));
        
        for (auto& [ConnectionId, Connection] : Connections)
        {
            if (ConnectionId != ExcludeConnectionId && Connection->IsConnected())
            {
                Connection->Send(Packet.data(), Packet.size());
            }
        }
        
        UnregisterActor(ActorId);
    }
    
    // 获取连接数
    size_t GetConnectionCount() const { return Connections.size(); }
    
    // 获取Actor数
    size_t GetActorCount() const { return ReplicationMap.size(); }

private:
    void ProcessPendingUpdates()
    {
        // 实际发送待处理的消息
        // 这里可以优化为批量发送
    }
};
