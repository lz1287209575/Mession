#include "Servers/World/WorldServer.h"
#include "Common/Net/Rpc/RpcServerCall.h"
#include "Servers/App/ClientCallAsyncSupport.h"
#include "Servers/App/ClientCallForwarding.h"
#include "Servers/App/ServerRpcSupport.h"

namespace MWorldClientFlows
{
template<typename TResponse>
TResponse BuildClientFailureResponse(const FAppError& Error, const char* FallbackCode)
{
    TResponse Failed;
    Failed.Error = Error.Code.empty() ? (FallbackCode ? FallbackCode : "client_call_failed") : Error.Code;
    return Failed;
}

class FClientLoginWorkflow final
    : public MClientCallAsyncSupport::TClientCallWorkflow<FClientLoginWorkflow, FClientLoginResponse>
{
public:
    using TResponseType = FClientLoginResponse;

    FClientLoginWorkflow(
        MWorldLoginRpc* InLoginRpc,
        MWorldPlayerServiceEndpoint* InPlayerService,
        FClientLoginRequest InRequest,
        uint64 InGatewayConnectionId)
        : LoginRpc(InLoginRpc)
        , PlayerService(InPlayerService)
        , Request(std::move(InRequest))
        , GatewayConnectionId(InGatewayConnectionId)
    {
    }

protected:
    void OnStart() override
    {
        if (!LoginRpc || !PlayerService)
        {
            Fail("client_login_dependencies_missing", "Client_Login");
            return;
        }

        FLoginIssueSessionRequest IssueRequest;
        IssueRequest.PlayerId = Request.PlayerId;
        IssueRequest.GatewayConnectionId = GatewayConnectionId;
        Continue(LoginRpc->IssueSession(IssueRequest), &FClientLoginWorkflow::OnSessionIssued);
    }

private:
    void OnSessionIssued(const FLoginIssueSessionResponse& LoginResponse)
    {
        SessionKey = LoginResponse.SessionKey;

        FPlayerEnterWorldRequest EnterRequest;
        EnterRequest.PlayerId = Request.PlayerId;
        EnterRequest.GatewayConnectionId = GatewayConnectionId;
        EnterRequest.SessionKey = SessionKey;
        Continue(PlayerService->PlayerEnterWorld(EnterRequest), &FClientLoginWorkflow::OnPlayerEnteredWorld);
    }

    void OnPlayerEnteredWorld(const FPlayerEnterWorldResponse&)
    {
        FClientLoginResponse Response;
        Response.bSuccess = true;
        Response.PlayerId = Request.PlayerId;
        Response.SessionKey = SessionKey;
        Succeed(std::move(Response));
    }

    MWorldLoginRpc* LoginRpc = nullptr;
    MWorldPlayerServiceEndpoint* PlayerService = nullptr;
    FClientLoginRequest Request;
    uint64 GatewayConnectionId = 0;
    uint32 SessionKey = 0;
};
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

    RegisterRpcTransport(EServerType::Login, LoginServerConn);
    RegisterRpcTransport(EServerType::Mgo, MgoServerConn);
    RegisterRpcTransport(EServerType::Scene, SceneServerConn);
    RegisterRpcTransport(EServerType::Router, RouterServerConn);
    PlayerService->Initialize(&OnlinePlayers, &PersistenceSubsystem, LoginRpc, MgoRpc, SceneRpc, RouterRpc);

    return true;
}

void MWorldServer::Tick()
{
    for (const auto& [PlayerId, Session] : OnlinePlayers)
    {
        (void)PlayerId;
        (void)PersistenceSubsystem.EnqueueRootIfDirty(Session);
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

    for (auto& [PlayerId, Session] : OnlinePlayers)
    {
        (void)PlayerId;
        DestroyMObject(Session);
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

    return PlayerService->PlayerEnterWorld(Request);
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

MFuture<TResult<FPlayerLogoutResponse, FAppError>> MWorldServer::PlayerLogout(const FPlayerLogoutRequest& Request)
{
    if (!PlayerService)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerLogoutResponse>(
            "world_player_service_missing",
            "PlayerLogout");
    }

    return PlayerService->PlayerLogout(Request);
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

    return PlayerService->PlayerSwitchScene(Request);
}

void MWorldServer::Client_Login(FClientLoginRequest& Request, FClientLoginResponse& Response)
{
    const uint64 GatewayConnectionId = GetCurrentClientConnectionId();
    if (GatewayConnectionId == 0)
    {
        Response.Error = "client_context_missing";
        return;
    }

    const SClientCallContext Context = CaptureCurrentClientCallContext();
    if (!Context.IsValid())
    {
        Response.Error = "client_call_context_missing";
        return;
    }

    (void)MClientCallAsyncSupport::StartDeferred<FClientLoginResponse>(
        Context,
        StartClientLoginFlow(Request, GatewayConnectionId),
        [](const FAppError& Error)
        {
            return MWorldClientFlows::BuildClientFailureResponse<FClientLoginResponse>(Error, "client_login_failed");
        });
}

void MWorldServer::Client_FindPlayer(FClientFindPlayerRequest& Request, FClientFindPlayerResponse& Response)
{
    const SClientCallContext Context = CaptureCurrentClientCallContext();
    if (!Context.IsValid())
    {
        Response.Error = "client_call_context_missing";
        return;
    }

    (void)MClientCallAsyncSupport::StartDeferred<FClientFindPlayerResponse>(
        Context,
        StartClientFindPlayerFlow(Request),
        [](const FAppError& Error)
        {
            return MWorldClientFlows::BuildClientFailureResponse<FClientFindPlayerResponse>(Error, "player_find_failed");
        });
}

void MWorldServer::Client_Logout(FClientLogoutRequest& Request, FClientLogoutResponse& Response)
{
    const SClientCallContext Context = CaptureCurrentClientCallContext();
    if (!Context.IsValid())
    {
        Response.Error = "client_call_context_missing";
        return;
    }

    (void)MClientCallAsyncSupport::StartDeferred<FClientLogoutResponse>(
        Context,
        StartClientLogoutFlow(Request),
        [](const FAppError& Error)
        {
            return MWorldClientFlows::BuildClientFailureResponse<FClientLogoutResponse>(Error, "player_logout_failed");
        });
}

void MWorldServer::Client_SwitchScene(
    FClientSwitchSceneRequest& Request,
    FClientSwitchSceneResponse& Response)
{
    const SClientCallContext Context = CaptureCurrentClientCallContext();
    if (!Context.IsValid())
    {
        Response.Error = "client_call_context_missing";
        return;
    }

    (void)MClientCallAsyncSupport::StartDeferred<FClientSwitchSceneResponse>(
        Context,
        StartClientSwitchSceneFlow(Request),
        [](const FAppError& Error)
        {
            return MWorldClientFlows::BuildClientFailureResponse<FClientSwitchSceneResponse>(Error, "player_switch_scene_failed");
        });
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

    return MClientCallForwarding::ExecuteForwardedClientCall(this, Request);
}

MFuture<TResult<FClientLoginResponse, FAppError>> MWorldServer::StartClientLoginFlow(
    const FClientLoginRequest& Request,
    uint64 GatewayConnectionId)
{
    if (Request.PlayerId == 0)
    {
        return MClientCallAsyncSupport::MakeErrorFuture<FClientLoginResponse>("player_id_required", "Client_Login");
    }

    if (GatewayConnectionId == 0)
    {
        return MClientCallAsyncSupport::MakeErrorFuture<FClientLoginResponse>("gateway_connection_id_required", "Client_Login");
    }

    if (!LoginRpc || !LoginRpc->IsAvailable())
    {
        return MClientCallAsyncSupport::MakeErrorFuture<FClientLoginResponse>("login_server_unavailable", "Client_Login");
    }

    if (!PlayerService)
    {
        return MClientCallAsyncSupport::MakeErrorFuture<FClientLoginResponse>("world_player_service_missing", "Client_Login");
    }

    return MClientCallAsyncSupport::StartWorkflow<MWorldClientFlows::FClientLoginWorkflow>(
        LoginRpc,
        PlayerService,
        Request,
        GatewayConnectionId);
}

MFuture<TResult<FClientFindPlayerResponse, FAppError>> MWorldServer::StartClientFindPlayerFlow(
    const FClientFindPlayerRequest& Request)
{
    if (Request.PlayerId == 0)
    {
        return MClientCallAsyncSupport::MakeErrorFuture<FClientFindPlayerResponse>("player_id_required", "Client_FindPlayer");
    }

    if (!PlayerService)
    {
        return MClientCallAsyncSupport::MakeErrorFuture<FClientFindPlayerResponse>("world_player_service_missing", "Client_FindPlayer");
    }

    FPlayerFindRequest FindRequest;
    FindRequest.PlayerId = Request.PlayerId;
    return MClientCallAsyncSupport::Map(
        PlayerService->PlayerFind(FindRequest),
        [](const FPlayerFindResponse& FindValue)
        {
            FClientFindPlayerResponse Response;
            Response.bFound = FindValue.bFound;
            Response.PlayerId = FindValue.PlayerId;
            Response.GatewayConnectionId = FindValue.GatewayConnectionId;
            Response.SceneId = FindValue.SceneId;
            return Response;
        });
}

MFuture<TResult<FClientLogoutResponse, FAppError>> MWorldServer::StartClientLogoutFlow(
    const FClientLogoutRequest& Request)
{
    if (Request.PlayerId == 0)
    {
        return MClientCallAsyncSupport::MakeErrorFuture<FClientLogoutResponse>("player_id_required", "Client_Logout");
    }

    if (!PlayerService)
    {
        return MClientCallAsyncSupport::MakeErrorFuture<FClientLogoutResponse>("world_player_service_missing", "Client_Logout");
    }

    FPlayerLogoutRequest LogoutRequest;
    LogoutRequest.PlayerId = Request.PlayerId;
    return MClientCallAsyncSupport::Map(
        PlayerService->PlayerLogout(LogoutRequest),
        [](const FPlayerLogoutResponse& LogoutValue)
        {
            FClientLogoutResponse Response;
            Response.bSuccess = true;
            Response.PlayerId = LogoutValue.PlayerId;
            return Response;
        });
}

MFuture<TResult<FClientSwitchSceneResponse, FAppError>> MWorldServer::StartClientSwitchSceneFlow(
    const FClientSwitchSceneRequest& Request)
{
    if (Request.PlayerId == 0)
    {
        return MClientCallAsyncSupport::MakeErrorFuture<FClientSwitchSceneResponse>("player_id_required", "Client_SwitchScene");
    }

    if (!PlayerService)
    {
        return MClientCallAsyncSupport::MakeErrorFuture<FClientSwitchSceneResponse>("world_player_service_missing", "Client_SwitchScene");
    }

    FPlayerSwitchSceneRequest SwitchRequest;
    SwitchRequest.PlayerId = Request.PlayerId;
    SwitchRequest.SceneId = Request.SceneId;
    return MClientCallAsyncSupport::Map(
        PlayerService->PlayerSwitchScene(SwitchRequest),
        [](const FPlayerSwitchSceneResponse& SwitchValue)
        {
            FClientSwitchSceneResponse Response;
            Response.bSuccess = true;
            Response.PlayerId = SwitchValue.PlayerId;
            Response.SceneId = SwitchValue.SceneId;
            return Response;
        });
}

void MWorldServer::HandlePeerPacket(uint64 /*ConnectionId*/, const TSharedPtr<INetConnection>& Connection, const TByteArray& Data)
{
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
