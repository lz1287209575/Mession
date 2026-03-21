#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/IO/Socket/Socket.h"
#include "Common/Net/HttpDebugServer.h"
#include "Common/Runtime/Log/Logger.h"
#include "Common/Net/NetServerBase.h"
#include "Common/Net/ServerConnection.h"
#include "Common/Runtime/Object/Object.h"
#include "Protocol/ServerMessages.h"
#include "Common/Runtime/Reflect/Reflection.h"
#include <thread>
#include <chrono>

// 场景服务器配置
struct SSceneConfig
{
    uint16 ListenPort = 8004;  // 世界服务器连接端口
    uint16 SceneId = 1;
    MString SceneName = "MainWorld";
    uint16 ZoneId = 0;
    MString RouterServerAddr = "127.0.0.1";
    uint16 RouterServerPort = 8005;
    MString WorldServerAddr = "127.0.0.1";
    uint16 WorldServerPort = 8003;
    SVector SceneSize = SVector(1000, 1000, 500);
    uint16 DebugHttpPort = 0;      // 调试 HTTP 端口（0 = 关闭）
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
    MString SceneName;
    SVector Size;
    
    // 场景中的实体
    TMap<uint64, SSceneEntity> Entities;
    
public:
    MScene(uint16 InId, const MString& InName, const SVector& InSize)
        : SceneId(InId), SceneName(InName), Size(InSize) {}
    
    uint16 GetSceneId() const { return SceneId; }
    const MString& GetSceneName() const { return SceneName; }
    
    void AddEntity(const SSceneEntity& Entity);
    void RemoveEntity(uint64 EntityId);
    void UpdateEntityPosition(uint64 EntityId, const SVector& NewPosition);

    void Tick(float DeltaTime);

    const TMap<uint64, SSceneEntity>& GetEntities() const { return Entities; }
};

// 场景服务器
MCLASS()
class MSceneServer : public MNetServerBase, public MObject
{
public:
    MGENERATED_BODY(MSceneServer, MObject, 0)

private:
    SSceneConfig Config;
    // 后端服务器连接管理器（Router/World 等）
    MServerConnectionManager BackendConnectionManager;
    TSharedPtr<MServerConnection> RouterServerConn;
    float WorldRouteQueryTimer = 0.0f;
    float LoadReportTimer = 0.0f;
    uint64 NextRouteRequestId = 1;
    TSharedPtr<MServerConnection> WorldServerConn;
    TMap<uint16, TSharedPtr<MScene>> Scenes;

    // 调试 HTTP 服务器
    TUniquePtr<MHttpDebugServer> DebugServer;

public:
    MSceneServer();
    ~MSceneServer() { Shutdown(); }
    using MObject::Tick;

    bool LoadConfig(const MString& ConfigPath);
    bool Init(int InPort = 0);
    void Tick();
    void Run() override { MNetServerBase::Run(); }

    uint16 GetListenPort() const override;
    void OnAccept(uint64 ConnId, TSharedPtr<INetConnection> Conn) override;
    void TickBackends() override;
    void ShutdownConnections() override;
    void OnRunStarted() override;
    MFUNCTION(NetServer, Rpc=ServerToServer, Reliable=true, Endpoint=Scene)
    void Rpc_OnRouterServerRegisterAck(uint8 Result);
    MFUNCTION(NetServer, Rpc=ServerToServer, Reliable=true, Endpoint=Scene)
    void Rpc_OnRouterRouteResponse(uint64 RequestId, uint8 RequestedTypeValue, uint64 PlayerId, bool bFound, uint32 ServerId, uint8 ServerTypeValue, const MString& ServerName, const MString& Address, uint16 Port, uint16 ZoneId);
    MFUNCTION(NetServer, Rpc=ServerToServer, Reliable=true, Endpoint=Scene)
    void Rpc_OnPlayerSwitchServer(uint64 PlayerId, uint16 SceneId, float X, float Y, float Z);
    MFUNCTION(NetServer, Rpc=ServerToServer, Reliable=true, Endpoint=Scene)
    void Rpc_OnPlayerLogout(uint64 PlayerId, uint16 SceneId);
    MFUNCTION(NetServer, Rpc=ServerToServer, Reliable=true, Endpoint=Scene)
    void Rpc_OnPlayerDataSync(uint64 PlayerId, uint16 SceneId, float X, float Y, float Z);

private:
    void ConnectToRouterServer();
    void ConnectToWorldServer();
    void HandleRouterServerPacket(uint8 PacketType, const TByteArray& Data);
    void SendRouterRegister();
    void QueryWorldServerRoute();
    void SendLoadReport();
    void ApplyWorldServerRoute(uint32 ServerId, const MString& ServerName, const MString& Address, uint16 Port);
    void HandleWorldPacket(uint8 PacketType, const TByteArray& Data);
    void CreateDefaultScenes();
    TSharedPtr<MScene> GetScene(uint16 SceneId);
    MString BuildDebugStatusJson() const;

    void OnRouter_ServerRegisterAck(const SNodeRegisterAckMessage& Message);
    void OnRouter_RouteResponse(const SRouteResponseMessage& Message);

};
