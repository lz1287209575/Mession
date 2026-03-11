#pragma once

#include "../../Core/NetCore.h"
#include "../../Core/Socket.h"
#include "../../Common/Logger.h"
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

// 世界服务器
class MWorldServer
{
private:
    int32 ListenSocket = -1;
    bool bRunning = false;
    
    // 配置
    SWorldConfig Config;
    
    // 网关连接
    TMap<uint64, TSharedPtr<MTcpConnection>> GatewayConnections;
    uint64 NextConnectionId = 1;
    
    // 场景服务器连接
    TMap<uint16, TSharedPtr<MTcpConnection>> SceneServers;  // SceneId -> Connection
    uint16 NextSceneId = 1;
    
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
    
    // 玩家管理
    void AddPlayer(uint64 PlayerId, const FString& Name, uint64 ConnectionId);
    void RemovePlayer(uint64 PlayerId);
    SPlayer* GetPlayerById(uint64 PlayerId);
    SPlayer* GetPlayerByConnection(uint64 ConnectionId);
    
    // 游戏逻辑
    void UpdateGameLogic(float DeltaTime);
};
