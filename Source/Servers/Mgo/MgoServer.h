#pragma once

#include "Core/Net/NetCore.h"
#include "Core/Net/Socket.h"
#include "Core/Net/HttpDebugServer.h"
#include "Core/Concurrency/ThreadPool.h"
#include "Common/Logger.h"
#include "Common/NetServerBase.h"
#include "Common/ServerConnection.h"
#include "Common/ServerMessages.h"
#include "Servers/Mgo/MgoRpcService.h"

struct SMgoConfig
{
    uint16 ListenPort = 8006;
    FString RouterServerAddr = "127.0.0.1";
    uint16 RouterServerPort = 8005;
    FString ServerName = "Mgo01";
    uint16 ZoneId = 0;
    uint16 DebugHttpPort = 0;
    bool EnableMongoStorage = false;
    FString MongoUri = "mongodb://127.0.0.1:27017";
    FString MongoDatabase = "mession";
    FString MongoCollection = "world_snapshots";
    uint32 MongoDbWorkers = 1;
};

struct SMgoPeer
{
    TSharedPtr<INetConnection> Connection;
    bool bAuthenticated = false;
    uint32 ServerId = 0;
    EServerType ServerType = EServerType::Unknown;
    FString ServerName;
};

MCLASS()
class MMgoServer : public MNetServerBase, public MReflectObject
{
public:
    MGENERATED_BODY(MMgoServer, MReflectObject, 0)

private:
    SMgoConfig Config;
    TMap<uint64, SMgoPeer> BackendConnections;
    TSharedPtr<MServerConnection> RouterServerConn;
    MMgoService MgoService;
    MServerMessageDispatcher BackendMessageDispatcher;
    MServerMessageDispatcher RouterMessageDispatcher;
    TUniquePtr<MHttpDebugServer> DebugServer;
    TUniquePtr<MThreadPool> DbThreadPool;
    uint64 PersistRequestCount = 0;
    uint64 PersistBytesTotal = 0;
    uint64 PersistMongoSuccess = 0;
    uint64 PersistMongoFailed = 0;
    uint64 LoadRequestCount = 0;
    uint64 LoadMongoSuccess = 0;
    uint64 LoadMongoFailed = 0;

public:
    MMgoServer() = default;
    ~MMgoServer() { Shutdown(); }

    bool LoadConfig(const FString& ConfigPath);
    bool Init(int InPort = 0);
    void Tick();
    void Run() override { MNetServerBase::Run(); }

    uint16 GetListenPort() const override;
    void OnAccept(uint64 ConnId, TSharedPtr<INetConnection> Conn) override;
    void TickBackends() override;
    void ShutdownConnections() override;
    void OnRunStarted() override;

    void Rpc_OnPersistSnapshot(uint64 ObjectId, uint16 ClassId, uint32 OwnerWorldId, uint64 RequestId, uint64 Version, const FString& ClassName, const FString& SnapshotHex);
    void Rpc_OnLoadSnapshotRequest(uint64 RequestId, uint64 ObjectId);
    MFUNCTION(NetServer, Rpc=ServerToServer, Reliable=true)
    void Rpc_OnRouterServerRegisterAck(uint8 Result);

private:
    void HandleBackendPacket(uint64 ConnectionId, const TArray& Data);
    bool SendServerMessage(uint64 ConnectionId, uint8 Type, const TArray& Payload);
    template<typename TMessage>
    bool SendServerMessage(uint64 ConnectionId, EServerMessageType Type, const TMessage& Message)
    {
        return SendServerMessage(ConnectionId, static_cast<uint8>(Type), BuildPayload(Message));
    }

    void HandleRouterServerMessage(uint8 Type, const TArray& Data);
    void SendRouterRegister();
    FString BuildDebugStatusJson() const;
    void InitBackendMessageHandlers();
    void InitRouterMessageHandlers();
    void OnBackend_ServerHandshake(uint64 ConnectionId, const SServerHandshakeMessage& Message);
    void OnBackend_Heartbeat(uint64 ConnectionId, const SHeartbeatMessage& Message);
    void OnRouter_ServerRegisterAck(const SServerRegisterAckMessage& Message);
    void SendPersistSnapshotResultToWorlds(uint32 OwnerWorldId, uint64 RequestId, uint64 ObjectId, uint64 Version, bool bSuccess, const FString& Reason);
    void SendLoadSnapshotResponseToWorlds(uint64 RequestId, uint64 ObjectId, bool bFound, uint16 ClassId, const FString& ClassName, const FString& SnapshotHex);
};
