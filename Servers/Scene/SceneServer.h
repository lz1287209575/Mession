#pragma once

#include "../../Core/NetCore.h"
#include "../../Core/Socket.h"
#include "../../Common/Logger.h"
#include <map>
#include <memory>
#include <vector>
#include <thread>
#include <chrono>

// 场景服务器配置
struct FSceneConfig
{
    uint16 ListenPort = 8004;  // 世界服务器连接端口
    uint16 SceneId = 1;
    FString SceneName = "MainWorld";
    FVector SceneSize = FVector(1000, 1000, 500);
};

// 场景中的实体
struct FSceneEntity
{
    uint64 EntityId;
    FVector Position;
    FRotator Rotation;
    uint32 ActorId;  // 对应的Actor ID
};

// 场景
class FScene
{
private:
    uint16 SceneId;
    FString SceneName;
    FVector Size;
    
    // 场景中的实体
    std::map<uint64, FSceneEntity> Entities;
    
public:
    FScene(uint16 InId, const FString& InName, const FVector& InSize)
        : SceneId(InId), SceneName(InName), Size(InSize) {}
    
    uint16 GetSceneId() const { return SceneId; }
    const FString& GetSceneName() const { return SceneName; }
    
    void AddEntity(const FSceneEntity& Entity);
    void RemoveEntity(uint64 EntityId);
    void UpdateEntityPosition(uint64 EntityId, const FVector& NewPosition);
    
    const std::map<uint64, FSceneEntity>& GetEntities() const { return Entities; }
};

// 场景服务器
class FSceneServer
{
private:
    int32 ListenSocket = -1;
    bool bRunning = false;
    
    // 配置
    FSceneConfig Config;
    
    // 世界服务器连接
    std::shared_ptr<FTcpConnection> WorldServerConn;
    
    // 场景管理
    std::map<uint16, std::shared_ptr<FScene>> Scenes;
    
public:
    FSceneServer();
    ~FSceneServer() { Shutdown(); }
    
    bool Init(int InPort);
    void Shutdown();
    void Tick();
    void Run();
    
private:
    void ConnectToWorldServer();
    void ProcessWorldServerMessages();
    void HandleWorldPacket(const TArray& Data);
    
    // 场景管理
    void CreateDefaultScenes();
    std::shared_ptr<FScene> GetScene(uint16 SceneId);
};
