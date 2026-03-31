#include "Servers/World/WorldServer.h"
#include "Common/Net/Rpc/RpcClientCall.h"
#include "Common/Net/Rpc/RpcServerCall.h"
#include "Servers/App/ClientCallForwarding.h"
#include "Servers/App/ServerRpcSupport.h"

namespace
{
bool CanReceiveSceneDownlink(const MPlayer* Player)
{
    if (!Player)
    {
        return false;
    }

    const MPlayerSession* Session = Player->GetSession();
    return Session && Session->GatewayConnectionId != 0;
}

bool TryBuildSceneStateMessage(const MPlayer* Player, SPlayerSceneStateMessage& OutMessage)
{
    if (!Player)
    {
        return false;
    }

    const MPlayerPawn* Pawn = Player->GetPawn();
    if (!Pawn || !Pawn->IsSpawned() || Pawn->SceneId == 0)
    {
        return false;
    }

    OutMessage.PlayerId = Player->PlayerId;
    OutMessage.SceneId = static_cast<uint16>(Pawn->SceneId);
    OutMessage.X = Pawn->X;
    OutMessage.Y = Pawn->Y;
    OutMessage.Z = Pawn->Z;
    return true;
}
}

bool MWorldServer::LoadConfig(const MString& /*ConfigPath*/)
{
    return true;
}

bool MWorldServer::Init(int InPort)
{
    if (InPort > 0)
    {
        Config.ListenPort = static_cast<uint16>(InPort);
    }

    bRunning = true;
    MLogger::LogStartupBanner("WorldServer", Config.ListenPort, 0);
    MServerConnection::SetLocalInfo(3, EServerType::World, "WorldSkeleton");
    PersistenceSubsystem.SetOwnerServerId(3);

    LoginServerConn = BackendConnectionManager.AddServer(
        SServerConnectionConfig(2, EServerType::Login, "LoginSkeleton", Config.LoginServerAddr, Config.LoginServerPort));
    SceneServerConn = BackendConnectionManager.AddServer(
        SServerConnectionConfig(4, EServerType::Scene, "SceneSkeleton", Config.SceneServerAddr, Config.SceneServerPort));
    RouterServerConn = BackendConnectionManager.AddServer(
        SServerConnectionConfig(5, EServerType::Router, "RouterSkeleton", Config.RouterServerAddr, Config.RouterServerPort));
    MgoServerConn = BackendConnectionManager.AddServer(
        SServerConnectionConfig(6, EServerType::Mgo, "MgoSkeleton", Config.MgoServerAddr, Config.MgoServerPort));

    LoginServerConn->SetOnMessage([this](auto, uint8 PacketType, const TByteArray& Data)
    {
        HandleBackendPacket(PacketType, Data, "Login");
    });
    SceneServerConn->SetOnMessage([this](auto, uint8 PacketType, const TByteArray& Data)
    {
        HandleBackendPacket(PacketType, Data, "Scene");
    });
    RouterServerConn->SetOnMessage([this](auto, uint8 PacketType, const TByteArray& Data)
    {
        HandleBackendPacket(PacketType, Data, "Router");
    });
    MgoServerConn->SetOnMessage([this](auto, uint8 PacketType, const TByteArray& Data)
    {
        HandleBackendPacket(PacketType, Data, "Mgo");
    });

    LoginServerConn->Connect();
    SceneServerConn->Connect();
    RouterServerConn->Connect();
    MgoServerConn->Connect();

    if (!LoginRpc)
    {
        LoginRpc = NewMObject<MWorldLoginRpc>(this, "LoginRpc");
    }
    if (!MgoRpc)
    {
        MgoRpc = NewMObject<MWorldMgoRpc>(this, "MgoRpc");
    }
    if (!SceneRpc)
    {
        SceneRpc = NewMObject<MWorldSceneRpc>(this, "SceneRpc");
    }
    if (!RouterRpc)
    {
        RouterRpc = NewMObject<MWorldRouterRpc>(this, "RouterRpc");
    }
    if (!PlayerService)
    {
        PlayerService = NewMObject<MWorldPlayerServiceEndpoint>(this, "PlayerService");
    }
    if (!ObjectProxyService)
    {
        ObjectProxyService = NewMObject<MObjectProxyServiceEndpoint>(this, "ObjectProxyService");
    }
    if (!ClientService)
    {
        ClientService = NewMObject<MWorldClientServiceEndpoint>(this, "ClientService");
    }

    RegisterRpcTransport(EServerType::Login, LoginServerConn);
    RegisterRpcTransport(EServerType::Mgo, MgoServerConn);
    RegisterRpcTransport(EServerType::Scene, SceneServerConn);
    RegisterRpcTransport(EServerType::Router, RouterServerConn);
    if (!PlayerRootResolver)
    {
        PlayerRootResolver = std::make_unique<FPlayerObjectProxyRootResolver>(&OnlinePlayers);
    }
    else
    {
        PlayerRootResolver->SetOnlinePlayers(&OnlinePlayers);
    }
    ObjectProxyRegistry.RegisterResolver(PlayerRootResolver.get());
    ObjectProxyService->Initialize(&ObjectProxyRegistry);
    PlayerService->Initialize(&OnlinePlayers, &PersistenceSubsystem, LoginRpc, MgoRpc, SceneRpc, RouterRpc);
    ClientService->Initialize(this, LoginRpc);

    return true;
}

void MWorldServer::Tick()
{
    for (const auto& [PlayerId, Player] : OnlinePlayers)
    {
        (void)PlayerId;
        (void)PersistenceSubsystem.EnqueueRootIfDirty(Player);
    }

    (void)PersistenceSubsystem.Flush(64);
}

uint16 MWorldServer::GetListenPort() const
{
    return Config.ListenPort;
}

void MWorldServer::OnAccept(uint64 ConnId, TSharedPtr<INetConnection> Conn)
{
    PeerConnections[ConnId] = Conn;
    LOG_INFO("World skeleton accepted connection %llu", static_cast<unsigned long long>(ConnId));
    EventLoop.RegisterConnection(
        ConnId,
        Conn,
        [this, Conn](uint64 ConnectionId, const TByteArray& Payload)
        {
            HandlePeerPacket(ConnectionId, Conn, Payload);
        },
        [this](uint64 ConnectionId)
        {
            PeerConnections.erase(ConnectionId);
        });
}

void MWorldServer::TickBackends()
{
    BackendConnectionManager.Tick(0.1f);
}

void MWorldServer::ShutdownConnections()
{
    for (auto& [ConnId, Conn] : PeerConnections)
    {
        (void)ConnId;
        if (Conn)
        {
            Conn->Close();
        }
    }
    PeerConnections.clear();

    for (auto& [PlayerId, Player] : OnlinePlayers)
    {
        (void)PlayerId;
        DestroyMObject(Player);
    }
    OnlinePlayers.clear();

    BackendConnectionManager.DisconnectAll();
    ClearRpcTransports();
    LoginServerConn.reset();
    SceneServerConn.reset();
    RouterServerConn.reset();
    MgoServerConn.reset();
}

void MWorldServer::OnRunStarted()
{
    LOG_INFO("World skeleton running on port %u", static_cast<unsigned>(Config.ListenPort));
}

MFuture<TResult<FPlayerEnterWorldResponse, FAppError>> MWorldServer::PlayerEnterWorld(
    const FPlayerEnterWorldRequest& Request)
{
    if (!PlayerService)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerEnterWorldResponse>(
            "world_player_service_missing",
            "PlayerEnterWorld");
    }

    MFuture<TResult<FPlayerEnterWorldResponse, FAppError>> Future = PlayerService->PlayerEnterWorld(Request);
    Future.Then([this, PlayerId = Request.PlayerId](MFuture<TResult<FPlayerEnterWorldResponse, FAppError>> Completed)
    {
        try
        {
            const TResult<FPlayerEnterWorldResponse, FAppError> Result = Completed.Get();
            if (Result.IsOk())
            {
                QueueScenePlayerEnterBroadcast(PlayerId);
            }
        }
        catch (...)
        {
        }
    });
    return Future;
}

MFuture<TResult<FPlayerFindResponse, FAppError>> MWorldServer::PlayerFind(const FPlayerFindRequest& Request)
{
    if (!PlayerService)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerFindResponse>(
            "world_player_service_missing",
            "PlayerFind");
    }

    return PlayerService->PlayerFind(Request);
}

MFuture<TResult<FPlayerUpdateRouteResponse, FAppError>> MWorldServer::PlayerUpdateRoute(
    const FPlayerUpdateRouteRequest& Request)
{
    if (!PlayerService)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerUpdateRouteResponse>(
            "world_player_service_missing",
            "PlayerUpdateRoute");
    }

    return PlayerService->PlayerUpdateRoute(Request);
}

MFuture<TResult<FPlayerMoveResponse, FAppError>> MWorldServer::PlayerMove(const FPlayerMoveRequest& Request)
{
    if (!PlayerService)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerMoveResponse>(
            "world_player_service_missing",
            "PlayerMove");
    }

    MFuture<TResult<FPlayerMoveResponse, FAppError>> Future = PlayerService->PlayerMove(Request);
    Future.Then([this, PlayerId = Request.PlayerId](MFuture<TResult<FPlayerMoveResponse, FAppError>> Completed)
    {
        try
        {
            const TResult<FPlayerMoveResponse, FAppError> Result = Completed.Get();
            if (Result.IsOk())
            {
                QueueScenePlayerUpdateBroadcast(PlayerId);
            }
        }
        catch (...)
        {
        }
    });
    return Future;
}

MFuture<TResult<FPlayerQueryProfileResponse, FAppError>> MWorldServer::PlayerQueryProfile(
    const FPlayerQueryProfileRequest& Request)
{
    if (!PlayerService)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerQueryProfileResponse>(
            "world_player_service_missing",
            "PlayerQueryProfile");
    }

    return PlayerService->PlayerQueryProfile(Request);
}

MFuture<TResult<FPlayerQueryPawnResponse, FAppError>> MWorldServer::PlayerQueryPawn(
    const FPlayerQueryPawnRequest& Request)
{
    if (!PlayerService)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerQueryPawnResponse>(
            "world_player_service_missing",
            "PlayerQueryPawn");
    }

    return PlayerService->PlayerQueryPawn(Request);
}

MFuture<TResult<FPlayerQueryInventoryResponse, FAppError>> MWorldServer::PlayerQueryInventory(
    const FPlayerQueryInventoryRequest& Request)
{
    if (!PlayerService)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerQueryInventoryResponse>(
            "world_player_service_missing",
            "PlayerQueryInventory");
    }

    return PlayerService->PlayerQueryInventory(Request);
}

MFuture<TResult<FPlayerQueryProgressionResponse, FAppError>> MWorldServer::PlayerQueryProgression(
    const FPlayerQueryProgressionRequest& Request)
{
    if (!PlayerService)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerQueryProgressionResponse>(
            "world_player_service_missing",
            "PlayerQueryProgression");
    }

    return PlayerService->PlayerQueryProgression(Request);
}

MFuture<TResult<FPlayerChangeGoldResponse, FAppError>> MWorldServer::PlayerChangeGold(
    const FPlayerChangeGoldRequest& Request)
{
    if (!PlayerService)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerChangeGoldResponse>(
            "world_player_service_missing",
            "PlayerChangeGold");
    }

    return PlayerService->PlayerChangeGold(Request);
}

MFuture<TResult<FPlayerEquipItemResponse, FAppError>> MWorldServer::PlayerEquipItem(
    const FPlayerEquipItemRequest& Request)
{
    if (!PlayerService)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerEquipItemResponse>(
            "world_player_service_missing",
            "PlayerEquipItem");
    }

    return PlayerService->PlayerEquipItem(Request);
}

MFuture<TResult<FPlayerGrantExperienceResponse, FAppError>> MWorldServer::PlayerGrantExperience(
    const FPlayerGrantExperienceRequest& Request)
{
    if (!PlayerService)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerGrantExperienceResponse>(
            "world_player_service_missing",
            "PlayerGrantExperience");
    }

    return PlayerService->PlayerGrantExperience(Request);
}

MFuture<TResult<FPlayerModifyHealthResponse, FAppError>> MWorldServer::PlayerModifyHealth(
    const FPlayerModifyHealthRequest& Request)
{
    if (!PlayerService)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerModifyHealthResponse>(
            "world_player_service_missing",
            "PlayerModifyHealth");
    }

    MFuture<TResult<FPlayerModifyHealthResponse, FAppError>> Future = PlayerService->PlayerModifyHealth(Request);
    Future.Then([this, PlayerId = Request.PlayerId](MFuture<TResult<FPlayerModifyHealthResponse, FAppError>> Completed)
    {
        try
        {
            const TResult<FPlayerModifyHealthResponse, FAppError> Result = Completed.Get();
            if (Result.IsOk())
            {
                QueueScenePlayerUpdateBroadcast(PlayerId);
            }
        }
        catch (...)
        {
        }
    });
    return Future;
}

MFuture<TResult<FPlayerLogoutResponse, FAppError>> MWorldServer::PlayerLogout(const FPlayerLogoutRequest& Request)
{
    if (!PlayerService)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerLogoutResponse>(
            "world_player_service_missing",
            "PlayerLogout");
    }

    uint32 SceneIdBeforeLogout = 0;
    if (const MPlayer* Player = FindPlayerById(Request.PlayerId))
    {
        SceneIdBeforeLogout = Player->ResolveCurrentSceneId();
    }

    MFuture<TResult<FPlayerLogoutResponse, FAppError>> Future = PlayerService->PlayerLogout(Request);
    Future.Then([this, PlayerId = Request.PlayerId, SceneIdBeforeLogout](MFuture<TResult<FPlayerLogoutResponse, FAppError>> Completed)
    {
        try
        {
            const TResult<FPlayerLogoutResponse, FAppError> Result = Completed.Get();
            if (Result.IsOk() && SceneIdBeforeLogout != 0)
            {
                QueueScenePlayerLeaveBroadcast(PlayerId, SceneIdBeforeLogout);
            }
        }
        catch (...)
        {
        }
    });
    return Future;
}

MFuture<TResult<FPlayerSwitchSceneResponse, FAppError>> MWorldServer::PlayerSwitchScene(
    const FPlayerSwitchSceneRequest& Request)
{
    if (!PlayerService)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerSwitchSceneResponse>(
            "world_player_service_missing",
            "PlayerSwitchScene");
    }

    uint32 PreviousSceneId = 0;
    if (const MPlayer* Player = FindPlayerById(Request.PlayerId))
    {
        PreviousSceneId = Player->ResolveCurrentSceneId();
    }

    MFuture<TResult<FPlayerSwitchSceneResponse, FAppError>> Future = PlayerService->PlayerSwitchScene(Request);
    Future.Then([this, PlayerId = Request.PlayerId, PreviousSceneId](MFuture<TResult<FPlayerSwitchSceneResponse, FAppError>> Completed)
    {
        try
        {
            const TResult<FPlayerSwitchSceneResponse, FAppError> Result = Completed.Get();
            if (!Result.IsOk())
            {
                return;
            }

            const uint32 NewSceneId = Result.GetValue().SceneId;
            if (PreviousSceneId != 0 && PreviousSceneId != NewSceneId)
            {
                QueueScenePlayerLeaveBroadcast(PlayerId, PreviousSceneId);
            }

            QueueScenePlayerEnterBroadcast(PlayerId);
        }
        catch (...)
        {
        }
    });
    return Future;
}

MPlayer* MWorldServer::FindPlayerById(uint64 PlayerId) const
{
    const auto It = OnlinePlayers.find(PlayerId);
    return It != OnlinePlayers.end() ? It->second : nullptr;
}

TSharedPtr<INetConnection> MWorldServer::ResolveGatewayPeerConnection() const
{
    for (const auto& [ConnectionId, Connection] : PeerConnections)
    {
        (void)ConnectionId;
        if (Connection && Connection->IsConnected())
        {
            return Connection;
        }
    }

    return nullptr;
}

void MWorldServer::QueueClientDownlink(uint64 GatewayConnectionId, uint16 FunctionId, const TByteArray& Payload) const
{
    if (GatewayConnectionId == 0 || FunctionId == 0)
    {
        return;
    }

    const TSharedPtr<INetConnection> GatewayPeerConnection = ResolveGatewayPeerConnection();
    if (!GatewayPeerConnection)
    {
        LOG_WARN("World missing Gateway peer connection for downlink: connection=%llu function_id=%u",
                 static_cast<unsigned long long>(GatewayConnectionId),
                 static_cast<unsigned>(FunctionId));
        return;
    }

    FClientDownlinkPushRequest Request;
    Request.GatewayConnectionId = GatewayConnectionId;
    Request.FunctionId = FunctionId;
    Request.Payload = Payload;

    CallServerFunction<SEmptyServerMessage>(GatewayPeerConnection, "MGatewayServer", "PushClientDownlink", Request)
        .Then([GatewayConnectionId, FunctionId](MFuture<TResult<SEmptyServerMessage, FAppError>> Completed)
        {
            try
            {
                const TResult<SEmptyServerMessage, FAppError> Result = Completed.Get();
                if (!Result.IsOk())
                {
                    LOG_WARN("World downlink push failed: connection=%llu function_id=%u code=%s",
                             static_cast<unsigned long long>(GatewayConnectionId),
                             static_cast<unsigned>(FunctionId),
                             Result.GetError().Code.c_str());
                }
            }
            catch (const std::exception& Ex)
            {
                LOG_WARN("World downlink push exception: connection=%llu function_id=%u error=%s",
                         static_cast<unsigned long long>(GatewayConnectionId),
                         static_cast<unsigned>(FunctionId),
                         Ex.what());
            }
            catch (...)
            {
                LOG_WARN("World downlink push unknown exception: connection=%llu function_id=%u",
                         static_cast<unsigned long long>(GatewayConnectionId),
                         static_cast<unsigned>(FunctionId));
            }
        });
}

void MWorldServer::QueueScenePlayerEnterBroadcast(uint64 PlayerId)
{
    MPlayer* SubjectPlayer = FindPlayerById(PlayerId);
    if (!SubjectPlayer || !CanReceiveSceneDownlink(SubjectPlayer))
    {
        return;
    }

    SPlayerSceneStateMessage SubjectState;
    if (!TryBuildSceneStateMessage(SubjectPlayer, SubjectState))
    {
        return;
    }

    const uint16 EnterFunctionId = MGET_STABLE_RPC_FUNCTION_ID("MClientDownlink", "Client_ScenePlayerEnter");
    if (EnterFunctionId == 0)
    {
        return;
    }

    const MPlayerSession* SubjectSession = SubjectPlayer->GetSession();
    if (!SubjectSession)
    {
        return;
    }

    const TByteArray SubjectPayload = BuildPayload(SubjectState);
    for (const auto& [OtherPlayerId, OtherPlayer] : OnlinePlayers)
    {
        if (OtherPlayerId == PlayerId || !CanReceiveSceneDownlink(OtherPlayer))
        {
            continue;
        }

        SPlayerSceneStateMessage OtherState;
        if (!TryBuildSceneStateMessage(OtherPlayer, OtherState))
        {
            continue;
        }

        if (OtherState.SceneId != SubjectState.SceneId)
        {
            continue;
        }

        QueueClientDownlink(OtherPlayer->GetSession()->GatewayConnectionId, EnterFunctionId, SubjectPayload);
        QueueClientDownlink(SubjectSession->GatewayConnectionId, EnterFunctionId, BuildPayload(OtherState));
    }
}

void MWorldServer::QueueScenePlayerUpdateBroadcast(uint64 PlayerId)
{
    MPlayer* SubjectPlayer = FindPlayerById(PlayerId);
    if (!SubjectPlayer)
    {
        return;
    }

    SPlayerSceneStateMessage SubjectState;
    if (!TryBuildSceneStateMessage(SubjectPlayer, SubjectState))
    {
        return;
    }

    const uint16 UpdateFunctionId = MGET_STABLE_RPC_FUNCTION_ID("MClientDownlink", "Client_ScenePlayerUpdate");
    if (UpdateFunctionId == 0)
    {
        return;
    }

    const TByteArray Payload = BuildPayload(SubjectState);
    for (const auto& [OtherPlayerId, OtherPlayer] : OnlinePlayers)
    {
        if (OtherPlayerId == PlayerId || !CanReceiveSceneDownlink(OtherPlayer))
        {
            continue;
        }

        if (OtherPlayer->ResolveCurrentSceneId() != SubjectState.SceneId)
        {
            continue;
        }

        QueueClientDownlink(OtherPlayer->GetSession()->GatewayConnectionId, UpdateFunctionId, Payload);
    }
}

void MWorldServer::QueueScenePlayerLeaveBroadcast(uint64 PlayerId, uint32 SceneId)
{
    if (SceneId == 0)
    {
        return;
    }

    const uint16 LeaveFunctionId = MGET_STABLE_RPC_FUNCTION_ID("MClientDownlink", "Client_ScenePlayerLeave");
    if (LeaveFunctionId == 0)
    {
        return;
    }

    SPlayerSceneLeaveMessage LeaveMessage;
    LeaveMessage.PlayerId = PlayerId;
    LeaveMessage.SceneId = static_cast<uint16>(SceneId);

    const TByteArray Payload = BuildPayload(LeaveMessage);
    for (const auto& [OtherPlayerId, OtherPlayer] : OnlinePlayers)
    {
        if (OtherPlayerId == PlayerId || !CanReceiveSceneDownlink(OtherPlayer))
        {
            continue;
        }

        if (OtherPlayer->ResolveCurrentSceneId() != SceneId)
        {
            continue;
        }

        QueueClientDownlink(OtherPlayer->GetSession()->GatewayConnectionId, LeaveFunctionId, Payload);
    }
}

MFuture<TResult<FForwardedClientCallResponse, FAppError>> MWorldServer::ForwardClientCall(
    const FForwardedClientCallRequest& Request)
{
    if (Request.GatewayConnectionId == 0)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FForwardedClientCallResponse>(
            "gateway_connection_id_required",
            "ForwardClientCall");
    }

    if (Request.ClientFunctionId == 0)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FForwardedClientCallResponse>(
            "client_function_id_required",
            "ForwardClientCall");
    }

    if (!ClientService)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FForwardedClientCallResponse>(
            "world_client_service_missing",
            "ForwardClientCall");
    }

    return MClientCallForwarding::ExecuteForwardedClientCall(ClientService, Request);
}

MFuture<TResult<FObjectProxyInvokeResponse, FAppError>> MWorldServer::InvokeObjectCall(
    const FObjectProxyInvokeRequest& Request)
{
    if (!ObjectProxyService)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FObjectProxyInvokeResponse>(
            "object_proxy_service_missing",
            "InvokeObjectCall");
    }

    return ObjectProxyService->InvokeObjectCall(Request);
}

void MWorldServer::HandlePeerPacket(uint64 /*ConnectionId*/, const TSharedPtr<INetConnection>& Connection, const TByteArray& Data)
{
    if (Data.empty())
    {
        return;
    }

    const uint8 PacketType = Data[0];
    if (PacketType == static_cast<uint8>(EServerMessageType::MT_FunctionResponse))
    {
        if (!HandleServerCallResponse(TByteArray(Data.begin() + 1, Data.end())))
        {
            LOG_WARN("World failed to handle peer function response");
        }
        return;
    }

    (void)MServerRpcSupport::DispatchServerCallPacket(this, Connection, Data);
}

void MWorldServer::HandleBackendPacket(uint8 PacketType, const TByteArray& Data, const char* PeerName)
{
    if (PacketType == static_cast<uint8>(EServerMessageType::MT_FunctionResponse))
    {
        if (!HandleServerCallResponse(Data))
        {
            LOG_WARN("World failed to handle backend function response from %s", PeerName ? PeerName : "backend");
        }
        return;
    }

    LOG_WARN("World received unsupported backend packet from %s: type=%u",
             PeerName ? PeerName : "backend",
             static_cast<unsigned>(PacketType));
}
