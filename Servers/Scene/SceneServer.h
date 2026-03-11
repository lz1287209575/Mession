#pragma once

#include "Core/NetCore.h"
#include "Core/Socket.h"
#include "Common/Logger.h"
#include "Common/ServerConnection.h"
#include <thread>
#include <chrono>

// 场景服务器配置
struct SSceneConfig
{
    uint16 ListenPort = 8004;  // 世界服务器连接端口
    uint16 SceneId = 1;
    FString SceneName = "MainWorld";
    FString RouterServerAddr = "127.0.0.1";
    uint16 RouterServerPort = 8005;
    FString WorldServerAddr = "127.0.0.1";
    uint16 WorldServerPort = 8003;
    SVector SceneSize = SVector(1000, 1000, 500);
};

// 场景中的实体
struct SSceneEntity
{
    uint64 EntityId;
    SVector Position;
    SRotator Rotation;
    uint32 ActorId;  // 对应的Actor ID
};

// 场景
class MScene
{
private:
    uint16 SceneId;
    FString SceneName;
    SVector Size;
    
    // 场景中的实体
    TMap<uint64, SSceneEntity> Entities;
    
public:
    MScene(uint16 InId, const FString& InName, const SVector& InSize)
        : SceneId(InId), SceneName(InName), Size(InSize) {}
    
    uint16 GetSceneId() const { return SceneId; }
    const FString& GetSceneName() const { return SceneName; }
    
    void AddEntity(const SSceneEntity& Entity);
    void RemoveEntity(uint64 EntityId);
    void UpdateEntityPosition(uint64 EntityId, const SVector& NewPosition);
    
    const TMap<uint64, SSceneEntity>& GetEntities() const { return Entities; }
};

// 场景服务器
class MSceneServer
{
private:
    TSocketFd ListenSocket = INVALID_SOCKET_FD;
    bool bRunning = false;
    
    // 配置
    SSceneConfig Config;
    
    // 世界服务器连接
    TSharedPtr<MServerConnection> RouterServerConn;
    float WorldRouteQueryTimer = 0.0f;
    uint64 NextRouteRequestId = 1;
    TSharedPtr<MServerConnection> WorldServerConn;
    
    // 场景管理
    TMap<uint16, TSharedPtr<MScene>> Scenes;
    
public:
    MSceneServer();
    ~MSceneServer() { Shutdown(); }
    
    bool Init(int InPort);
    void Shutdown();
    void Tick();
    void Run();
    
private:
    void ConnectToRouterServer();
    void ConnectToWorldServer();
    void HandleRouterServerMessage(uint8 Type, const TArray& Data);
    void SendRouterRegister();
    void QueryWorldServerRoute();
    void ApplyWorldServerRoute(uint32 ServerId, const FString& ServerName, const FString& Address, uint16 Port);
    void ProcessWorldServerMessages();
    void HandleWorldPacket(uint8 Type, const TArray& Data);
    
    // 场景管理
    void CreateDefaultScenes();
    TSharedPtr<MScene> GetScene(uint16 SceneId);
};
