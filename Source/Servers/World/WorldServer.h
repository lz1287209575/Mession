#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/Net/NetServerBase.h"
#include "Common/Net/ServerConnection.h"
#include "Common/Net/Rpc/RpcRuntimeContext.h"
#include "Common/Runtime/Concurrency/Promise.h"
#include "Common/Runtime/Object/Object.h"
#include "Common/Runtime/Object/Result.h"
#include "Common/Runtime/Persistence/PersistenceSubsystem.h"
#include "Protocol/Messages/Common/AppMessages.h"
#include "Protocol/Messages/Common/ClientDownlinkMessages.h"
#include "Protocol/Messages/Common/ControlPlaneMessages.h"
#include "Protocol/Messages/Common/ForwardedClientCallMessages.h"
#include "Protocol/Messages/Common/ObjectCallMessages.h"
#include "Servers/App/ObjectCallRegistry.h"
#include "Servers/App/ServerCallAsyncSupport.h"

struct SWorldConfig
{
    uint16 ListenPort = 8003;
    MString LoginServerAddr = "127.0.0.1";
    uint16 LoginServerPort = 8002;
    MString SceneServerAddr = "127.0.0.1";
    uint16 SceneServerPort = 8004;
    MString RouterServerAddr = "127.0.0.1";
    uint16 RouterServerPort = 8005;
    MString MgoServerAddr = "127.0.0.1";
    uint16 MgoServerPort = 8006;
};

class MPlayerService;
class MObjectCallRouter;
class MWorldClient;
class MWorldLogin;
class MWorldMgo;
class MWorldRouter;
class MWorldScene;

MCLASS(Type=Server)
class MWorldServer : public MNetServerBase, public MObject, public MServerRuntimeContext, public IObjectCallRegistryProvider
{
public:
    MGENERATED_BODY(MWorldServer, MObject, 0)
public:
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

    MFUNCTION(ServerCall)
    MFuture<TResult<FForwardedClientCallResponse, FAppError>> DispatchClientCall(
        const FForwardedClientCallRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FObjectCallResponse, FAppError>> DispatchObjectCall(const FObjectCallRequest& Request);

    MWorldClient* GetClient() const { return Client; }
    MPlayerService* GetPlayerService() const { return Player; }
    MWorldLogin* GetLogin() const { return Login; }
    MWorldMgo* GetMgo() const { return Mgo; }
    MWorldScene* GetScene() const { return Scene; }
    MWorldRouter* GetRouter() const { return Router; }
    MPersistenceSubsystem& GetPersistence() { return PersistenceSubsystem; }
    const MPersistenceSubsystem& GetPersistence() const { return PersistenceSubsystem; }
    MObjectCallRegistry* GetObjectCallRegistry() override { return &ObjectCallRegistry; }
    const MObjectCallRegistry* GetObjectCallRegistry() const override { return &ObjectCallRegistry; }
    void QueueClientNotify(uint64 GatewayConnectionId, uint16 FunctionId, const TByteArray& Payload) const;

private:
    void InitBackendConnections();
    void InitBackendHandlers();
    void ConnectBackends();
    void InitServices();
    void RegisterBackendTransports();

    TSharedPtr<INetConnection> ResolveGatewayConnection() const;
    void HandleGatewayPacket(uint64 ConnectionId, const TSharedPtr<INetConnection>& Connection, const TByteArray& Data);
    void HandleBackendPacket(uint8 PacketType, const TByteArray& Data, const char* ServerName);
    SWorldConfig Config;
    TMap<uint64, TSharedPtr<INetConnection>> GatewayConnections;
    MServerConnectionManager BackendConnectionManager;
    TSharedPtr<MServerConnection> LoginServerConn;
    TSharedPtr<MServerConnection> SceneServerConn;
    TSharedPtr<MServerConnection> RouterServerConn;
    TSharedPtr<MServerConnection> MgoServerConn;
    MWorldClient* Client = nullptr;
    MPlayerService* Player = nullptr;
    MWorldLogin* Login = nullptr;
    MWorldMgo* Mgo = nullptr;
    MWorldScene* Scene = nullptr;
    MWorldRouter* Router = nullptr;
    MObjectCallRouter* ObjectCallRouter = nullptr;
    MPersistenceSubsystem PersistenceSubsystem;
    MObjectCallRegistry ObjectCallRegistry;
};

