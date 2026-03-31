#pragma once

#include "Common/Runtime/MLib.h"
#include "Common/IO/Socket/Socket.h"
#include "Common/Net/NetServerBase.h"
#include "Common/Net/ServerConnection.h"
#include "Common/Net/Rpc/RpcRuntimeContext.h"
#include "Common/Runtime/Concurrency/Promise.h"
#include "Common/Runtime/Log/Logger.h"
#include "Common/Runtime/Object/Object.h"
#include "Common/Runtime/Object/Result.h"
#include "Common/Runtime/Persistence/PersistenceSubsystem.h"
#include "Protocol/Messages/Common/AppMessages.h"
#include "Protocol/Messages/Common/ClientDownlinkMessages.h"
#include "Protocol/Messages/Common/ControlPlaneMessages.h"
#include "Protocol/Messages/Common/ForwardedClientCallMessages.h"
#include "Protocol/Messages/Common/ObjectProxyMessages.h"
#include "Protocol/Messages/Gateway/GatewayClientMessages.h"
#include "Protocol/Messages/Scene/SceneSyncMessages.h"
#include "Protocol/Messages/World/WorldPlayerMessages.h"
#include "Protocol/Messages/Auth/AuthSessionMessages.h"
#include "Protocol/Messages/Mgo/MgoPlayerStateMessages.h"
#include "Protocol/Messages/Router/RouterServiceMessages.h"
#include "Protocol/Messages/Scene/SceneServiceMessages.h"
#include "Servers/App/ObjectProxyRegistry.h"
#include "Servers/App/ObjectProxyServiceEndpoint.h"
#include "Servers/World/Players/Player.h"
#include "Servers/World/Rpc/WorldBackendRpc.h"
#include "Servers/World/Services/WorldClientServiceEndpoint.h"
#include "Servers/World/Services/WorldPlayerServiceEndpoint.h"

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

class FPlayerObjectProxyRootResolver final : public IObjectProxyRootResolver
{
public:
    explicit FPlayerObjectProxyRootResolver(const TMap<uint64, MPlayer*>* InOnlinePlayers = nullptr)
        : OnlinePlayers(InOnlinePlayers)
    {
    }

    void SetOnlinePlayers(const TMap<uint64, MPlayer*>* InOnlinePlayers)
    {
        OnlinePlayers = InOnlinePlayers;
    }

    EObjectProxyRootType GetRootType() const override
    {
        return EObjectProxyRootType::Player;
    }

    EServerType GetOwnerServerType() const override
    {
        return EServerType::World;
    }

    MObject* ResolveRootObject(uint64 RootId) const override
    {
        if (!OnlinePlayers)
        {
            return nullptr;
        }

        const auto It = OnlinePlayers->find(RootId);
        return It != OnlinePlayers->end() ? It->second : nullptr;
    }

private:
    const TMap<uint64, MPlayer*>* OnlinePlayers = nullptr;
};

MCLASS(Type=Server)
class MWorldServer : public MNetServerBase, public MObject, public MServerRuntimeContext, public IObjectProxyRegistryProvider
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
    MFuture<TResult<FPlayerEnterWorldResponse, FAppError>> PlayerEnterWorld(const FPlayerEnterWorldRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FPlayerFindResponse, FAppError>> PlayerFind(const FPlayerFindRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FPlayerUpdateRouteResponse, FAppError>> PlayerUpdateRoute(const FPlayerUpdateRouteRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FPlayerMoveResponse, FAppError>> PlayerMove(const FPlayerMoveRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FPlayerQueryProfileResponse, FAppError>> PlayerQueryProfile(const FPlayerQueryProfileRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FPlayerQueryPawnResponse, FAppError>> PlayerQueryPawn(const FPlayerQueryPawnRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FPlayerQueryInventoryResponse, FAppError>> PlayerQueryInventory(const FPlayerQueryInventoryRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FPlayerQueryProgressionResponse, FAppError>> PlayerQueryProgression(
        const FPlayerQueryProgressionRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FPlayerChangeGoldResponse, FAppError>> PlayerChangeGold(const FPlayerChangeGoldRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FPlayerEquipItemResponse, FAppError>> PlayerEquipItem(const FPlayerEquipItemRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FPlayerGrantExperienceResponse, FAppError>> PlayerGrantExperience(
        const FPlayerGrantExperienceRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FPlayerModifyHealthResponse, FAppError>> PlayerModifyHealth(
        const FPlayerModifyHealthRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FPlayerLogoutResponse, FAppError>> PlayerLogout(const FPlayerLogoutRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FPlayerSwitchSceneResponse, FAppError>> PlayerSwitchScene(const FPlayerSwitchSceneRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FForwardedClientCallResponse, FAppError>> ForwardClientCall(
        const FForwardedClientCallRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FObjectProxyInvokeResponse, FAppError>> InvokeObjectCall(const FObjectProxyInvokeRequest& Request);

    MObjectProxyRegistry* GetObjectProxyRegistry() override { return &ObjectProxyRegistry; }
    const MObjectProxyRegistry* GetObjectProxyRegistry() const override { return &ObjectProxyRegistry; }

private:
    MPlayer* FindPlayerById(uint64 PlayerId) const;
    TSharedPtr<INetConnection> ResolveGatewayPeerConnection() const;
    void QueueClientDownlink(uint64 GatewayConnectionId, uint16 FunctionId, const TByteArray& Payload) const;
    void QueueScenePlayerEnterBroadcast(uint64 PlayerId);
    void QueueScenePlayerUpdateBroadcast(uint64 PlayerId);
    void QueueScenePlayerLeaveBroadcast(uint64 PlayerId, uint32 SceneId);

    void HandlePeerPacket(uint64 ConnectionId, const TSharedPtr<INetConnection>& Connection, const TByteArray& Data);
    void HandleBackendPacket(uint8 PacketType, const TByteArray& Data, const char* PeerName);
    SWorldConfig Config;
    TMap<uint64, MPlayer*> OnlinePlayers;
    TMap<uint64, TSharedPtr<INetConnection>> PeerConnections;
    MServerConnectionManager BackendConnectionManager;
    TSharedPtr<MServerConnection> LoginServerConn;
    TSharedPtr<MServerConnection> SceneServerConn;
    TSharedPtr<MServerConnection> RouterServerConn;
    TSharedPtr<MServerConnection> MgoServerConn;
    MWorldClientServiceEndpoint* ClientService = nullptr;
    MWorldPlayerServiceEndpoint* PlayerService = nullptr;
    MWorldLoginRpc* LoginRpc = nullptr;
    MWorldMgoRpc* MgoRpc = nullptr;
    MWorldSceneRpc* SceneRpc = nullptr;
    MWorldRouterRpc* RouterRpc = nullptr;
    MObjectProxyServiceEndpoint* ObjectProxyService = nullptr;
    MPersistenceSubsystem PersistenceSubsystem;
    MObjectProxyRegistry ObjectProxyRegistry;
    TUniquePtr<FPlayerObjectProxyRootResolver> PlayerRootResolver;
};
