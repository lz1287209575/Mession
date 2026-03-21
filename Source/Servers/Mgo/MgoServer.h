#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/IO/Socket/Socket.h"
#include "Common/Net/HttpDebugServer.h"
#include "Common/Runtime/Concurrency/ThreadPool.h"
#include "Common/Runtime/Log/Logger.h"
#include "Common/Net/NetServerBase.h"
#include "Common/Net/ServerConnection.h"
#include "Protocol/ServerMessages.h"

struct SMgoConfig
{
    uint16 ListenPort = 8006;
    MString RouterServerAddr = "127.0.0.1";
    uint16 RouterServerPort = 8005;
    MString ServerName = "Mgo01";
    uint16 ZoneId = 0;
    uint16 DebugHttpPort = 0;
    bool EnableMongoStorage = false;
    MString MongoUri = "mongodb://127.0.0.1:27017";
    MString MongoDatabase = "mession";
    MString MongoCollection = "world_snapshots";
    uint32 MongoDbWorkers = 1;
};

struct SMgoPeer
{
    TSharedPtr<INetConnection> Connection;
    bool bAuthenticated = false;
    uint32 ServerId = 0;
    EServerType ServerType = EServerType::Unknown;
    MString ServerName;
};

MCLASS()
class MMgoServer : public MNetServerBase, public MObject
{
public:
    MGENERATED_BODY(MMgoServer, MObject, 0)

private:
    SMgoConfig Config;
    TMap<uint64, SMgoPeer> BackendConnections;
    TSharedPtr<MServerConnection> RouterServerConn;
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
    MFUNCTION(NetServer, Rpc=ServerToServer, Reliable=true, Endpoint=Mgo)
    void Rpc_OnServerHandshake(uint32 ServerId, uint8 ServerTypeValue, const MString& ServerName);
    MFUNCTION(NetServer, Rpc=ServerToServer, Reliable=true, Endpoint=Mgo)
    void Rpc_OnHeartbeat(uint32 Sequence);

    MFUNCTION(NetServer, Rpc=ServerToServer, Reliable=true, Endpoint=Mgo)
    void Rpc_OnPersistSnapshot(uint64 ObjectId, uint16 ClassId, uint32 OwnerWorldId, uint64 RequestId, uint64 Version, const MString& ClassName, const MString& SnapshotHex);
    MFUNCTION(NetServer, Rpc=ServerToServer, Reliable=true, Endpoint=Mgo)
    void Rpc_OnLoadSnapshotRequest(uint64 RequestId, uint64 ObjectId);
    MFUNCTION(NetServer, Rpc=ServerToServer, Reliable=true)
    void Rpc_OnRouterServerRegisterAck(uint8 Result);

private:
    void HandleBackendPacket(uint64 ConnectionId, const TByteArray& Data);
    bool SendServerPacket(uint64 ConnectionId, uint8 PacketType, const TByteArray& Payload);
    template<typename TMessage>
    bool SendServerPacket(uint64 ConnectionId, EServerMessageType PacketType, const TMessage& Message)
    {
        return SendServerPacket(ConnectionId, static_cast<uint8>(PacketType), BuildPayload(Message));
    }

    void HandleRouterServerPacket(uint8 PacketType, const TByteArray& Data);
    void SendRouterRegister();
    MString BuildDebugStatusJson() const;
    void OnRouter_ServerRegisterAck(const SNodeRegisterAckMessage& Message);
    void SendPersistSnapshotResultToWorlds(uint32 OwnerWorldId, uint64 RequestId, uint64 ObjectId, uint64 Version, bool bSuccess, const MString& Reason);
    void SendLoadSnapshotResponseToWorlds(uint64 RequestId, uint64 ObjectId, bool bFound, uint16 ClassId, const MString& ClassName, const MString& SnapshotHex);
};
