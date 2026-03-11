#pragma once

#include "../../Core/NetCore.h"
#include "../../Core/Socket.h"
#include "../../Common/Logger.h"
#include "../../Common/ServerConnection.h"
#include "../../NetDriver/NetObject.h"
#include "../../NetDriver/ReplicationDriver.h"
#include <thread>
#include <chrono>

// 世界服务器配置
struct SWorldConfig
{
    uint16 ListenPort = 8003;      // 网关连接端口
    uint16 SceneServerPort = 8004; // 场景服务器端口
    FString ServerName = "World01";
    uint32 MaxPlayers = 10000;
};

// 玩家数据
struct SPlayer
{
    uint64 PlayerId;
    FString Name;
    uint64 ConnectionId;
    uint32 SessionKey;
    MActor* Character = nullptr;
    uint32 CurrentSceneId = 0;
    bool bOnline = false;
};

struct SBackendPeer
{
    TSharedPtr<MTcpConnection> Connection;
    bool bAuthenticated = false;
    uint32 ServerId = 0;
    EServerType ServerType = EServerType::Unknown;
    FString ServerName;
};

// 世界服务器
class MWorldServer
{
private:
    int32 ListenSocket = -1;
    bool bRunning = false;
    
    // 配置
    SWorldConfig Config;
    
    // 网关连接
    TMap<uint64, SBackendPeer> BackendConnections;
    uint64 NextConnectionId = 1;
    
    // 玩家管理
    TMap<uint64, SPlayer> Players;  // PlayerId -> Player
    TMap<uint64, uint64> ConnectionToPlayer;  // ConnectionId -> PlayerId
    
    // 复制系统
    MReplicationDriver* ReplicationDriver;
    
public:
    MWorldServer();
    ~MWorldServer() { Shutdown(); }
    
    bool Init(int InPort);
    void Shutdown();
    void Tick();
    void Run();
    
private:
    void AcceptConnections();
    void ProcessMessages();
    void HandlePacket(uint64 ConnectionId, const TArray& Data);
    void HandleGameplayPacket(uint64 ConnectionId, const TArray& Data);
    bool SendServerMessage(uint64 ConnectionId, uint8 Type, const TArray& Payload);
    void BroadcastToScenes(uint8 Type, const TArray& Payload);
    
    // 玩家管理
    void AddPlayer(uint64 PlayerId, const FString& Name, uint64 ConnectionId);
    void RemovePlayer(uint64 PlayerId);
    SPlayer* GetPlayerById(uint64 PlayerId);
    SPlayer* GetPlayerByConnection(uint64 ConnectionId);
    
    // 游戏逻辑
    void UpdateGameLogic(float DeltaTime);
};
