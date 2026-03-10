#pragma once

#include "../../Core/NetCore.h"
#include "../../Core/Socket.h"
#include "../../Common/Logger.h"
#include "../../NetDriver/NetObject.h"
#include "../../NetDriver/ReplicationDriver.h"
#include <map>
#include <memory>
#include <vector>
#include <thread>
#include <chrono>

// 世界服务器配置
struct FWorldConfig
{
    uint16 ListenPort = 8003;      // 网关连接端口
    uint16 SceneServerPort = 8004; // 场景服务器端口
    FString ServerName = "World01";
    uint32 MaxPlayers = 10000;
};

// 玩家数据
struct FPlayer
{
    uint64 PlayerId;
    FString Name;
    uint64 ConnectionId;
    uint32 SessionKey;
    AActor* Character = nullptr;
    uint32 CurrentSceneId = 0;
    bool bOnline = false;
};

// 世界服务器
class FWorldServer
{
private:
    int32 ListenSocket = -1;
    bool bRunning = false;
    
    // 配置
    FWorldConfig Config;
    
    // 网关连接
    std::map<uint64, std::shared_ptr<FTcpConnection>> GatewayConnections;
    uint64 NextConnectionId = 1;
    
    // 场景服务器连接
    std::map<uint16, std::shared_ptr<FTcpConnection>> SceneServers;  // SceneId -> Connection
    uint16 NextSceneId = 1;
    
    // 玩家管理
    std::map<uint64, FPlayer> Players;  // PlayerId -> Player
    std::map<uint64, uint64> ConnectionToPlayer;  // ConnectionId -> PlayerId
    
    // 复制系统
    UReplicationDriver* ReplicationDriver;
    
public:
    FWorldServer();
    ~FWorldServer() { Shutdown(); }
    
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
    FPlayer* GetPlayerById(uint64 PlayerId);
    FPlayer* GetPlayerByConnection(uint64 ConnectionId);
    
    // 游戏逻辑
    void UpdateGameLogic(float DeltaTime);
};
