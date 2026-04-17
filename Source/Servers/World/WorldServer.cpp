#include "Servers/World/WorldServer.h"

#include "Common/Net/Rpc/RpcServerCall.h"
#include "Common/Runtime/Log/Logger.h"
#include "Servers/App/ClientDispatch.h"
#include "Servers/App/ObjectCallRouter.h"
#include "Servers/App/ServerCallAsyncSupport.h"
#include "Servers/App/ServerRpcSupport.h"
#include "Servers/World/Backend/WorldLogin.h"
#include "Servers/World/Backend/WorldMgo.h"
#include "Servers/World/Backend/WorldRouter.h"
#include "Servers/World/Backend/WorldScene.h"
#include "Servers/World/Player/PlayerManager.h"
#include "Servers/World/Player/PlayerService.h"
#include "Servers/World/WorldClient.h"

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

    InitBackendConnections();
    InitBackendHandlers();
    ConnectBackends();
    InitServices();
    RegisterBackendTransports();
    PlayerManager->Initialize(this);
    Player->Initialize(this);
    ObjectCallRegistry.RegisterResolver(Player->GetCallRootResolver());
    ObjectCallRouter->Initialize(&ObjectCallRegistry);
    Client->Initialize(this, Login);

    return true;
}

void MWorldServer::Tick()
{
    if (Player)
    {
        Player->FlushPersistence();
    }
}

uint16 MWorldServer::GetListenPort() const
{
    return Config.ListenPort;
}

void MWorldServer::InitBackendConnections()
{
    LoginServerConn = BackendConnectionManager.AddServer(
        SServerConnectionConfig(2, EServerType::Login, "LoginSkeleton", Config.LoginServerAddr, Config.LoginServerPort));
    SceneServerConn = BackendConnectionManager.AddServer(
        SServerConnectionConfig(4, EServerType::Scene, "SceneSkeleton", Config.SceneServerAddr, Config.SceneServerPort));
    RouterServerConn = BackendConnectionManager.AddServer(
        SServerConnectionConfig(5, EServerType::Router, "RouterSkeleton", Config.RouterServerAddr, Config.RouterServerPort));
    MgoServerConn = BackendConnectionManager.AddServer(
        SServerConnectionConfig(6, EServerType::Mgo, "MgoSkeleton", Config.MgoServerAddr, Config.MgoServerPort));
}

void MWorldServer::InitBackendHandlers()
{
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
}

void MWorldServer::ConnectBackends()
{
    LoginServerConn->Connect();
    SceneServerConn->Connect();
    RouterServerConn->Connect();
    MgoServerConn->Connect();
}

void MWorldServer::InitServices()
{
    if (!Login)
    {
        Login = NewMObject<MWorldLogin>(this, "Login");
    }
    if (!Mgo)
    {
        Mgo = NewMObject<MWorldMgo>(this, "Mgo");
    }
    if (!Scene)
    {
        Scene = NewMObject<MWorldScene>(this, "Scene");
    }
    if (!Router)
    {
        Router = NewMObject<MWorldRouter>(this, "Router");
    }
    if (!ObjectCallRouter)
    {
        ObjectCallRouter = NewMObject<MObjectCallRouter>(this, "ObjectCallRouter");
    }
    if (!Player)
    {
        Player = NewMObject<MPlayerService>(this, "Player");
    }
    if (!PlayerManager)
    {
        PlayerManager = NewMObject<MPlayerManager>(this, "PlayerManager");
    }
    if (!Client)
    {
        Client = NewMObject<MWorldClient>(this, "Client");
    }
}

void MWorldServer::RegisterBackendTransports()
{
    RegisterRpcTransport(EServerType::Login, LoginServerConn);
    RegisterRpcTransport(EServerType::Mgo, MgoServerConn);
    RegisterRpcTransport(EServerType::Scene, SceneServerConn);
    RegisterRpcTransport(EServerType::Router, RouterServerConn);
}

void MWorldServer::OnAccept(uint64 ConnId, TSharedPtr<INetConnection> Conn)
{
    GatewayConnections[ConnId] = Conn;
    LOG_INFO("World skeleton accepted connection %llu", static_cast<unsigned long long>(ConnId));
    EventLoop.RegisterConnection(
        ConnId,
        Conn,
        [this, Conn](uint64 ConnectionId, const TByteArray& Payload)
        {
            HandleGatewayPacket(ConnectionId, Conn, Payload);
        },
        [this](uint64 ConnectionId)
        {
            GatewayConnections.erase(ConnectionId);
        });
}

void MWorldServer::TickBackends()
{
    BackendConnectionManager.Tick(0.1f);
}

void MWorldServer::ShutdownConnections()
{
    for (auto& [ConnId, Conn] : GatewayConnections)
    {
        (void)ConnId;
        if (Conn)
        {
            Conn->Close();
        }
    }
    GatewayConnections.clear();

    if (Player)
    {
        Player->ShutdownPlayers();
    }

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

TSharedPtr<INetConnection> MWorldServer::ResolveGatewayConnection() const
{
    for (const auto& [ConnectionId, Connection] : GatewayConnections)
    {
        (void)ConnectionId;
        if (Connection && Connection->IsConnected())
        {
            return Connection;
        }
    }

    return nullptr;
}

void MWorldServer::QueueClientNotify(uint64 GatewayConnectionId, uint16 FunctionId, const TByteArray& Payload) const
{
    if (GatewayConnectionId == 0 || FunctionId == 0)
    {
        return;
    }

    const TSharedPtr<INetConnection> GatewayConnection = ResolveGatewayConnection();
    if (!GatewayConnection)
    {
        LOG_WARN("World missing Gateway connection for downlink: connection=%llu function_id=%u",
                 static_cast<unsigned long long>(GatewayConnectionId),
                 static_cast<unsigned>(FunctionId));
        return;
    }

    FClientDownlinkPushRequest Request;
    Request.GatewayConnectionId = GatewayConnectionId;
    Request.FunctionId = FunctionId;
    Request.Payload = Payload;

    CallServerFunction<SEmptyServerMessage>(GatewayConnection, "MGatewayServer", "PushClientDownlink", Request)
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

MFuture<TResult<FForwardedClientCallResponse, FAppError>> MWorldServer::DispatchClientCall(
    const FForwardedClientCallRequest& Request)
{
    if (Request.GatewayConnectionId == 0)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FForwardedClientCallResponse>(
            "gateway_connection_id_required",
            "DispatchClientCall");
    }

    if (Request.ClientFunctionId == 0)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FForwardedClientCallResponse>(
            "client_function_id_required",
            "DispatchClientCall");
    }

    if (!Client)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FForwardedClientCallResponse>(
            "world_client_service_missing",
            "DispatchClientCall");
    }

    return MClientDispatch::DispatchCall(Client, Request);
}

MFuture<TResult<FObjectCallResponse, FAppError>> MWorldServer::DispatchObjectCall(
    const FObjectCallRequest& Request)
{
    if (!ObjectCallRouter)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FObjectCallResponse>(
            "object_call_service_missing",
            "DispatchObjectCall");
    }

    return ObjectCallRouter->DispatchObjectCall(Request);
}

void MWorldServer::HandleGatewayPacket(
    uint64 /*ConnectionId*/,
    const TSharedPtr<INetConnection>& Connection,
    const TByteArray& Data)
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
            LOG_WARN("World failed to handle Gateway function response");
        }
        return;
    }

    (void)MServerRpcSupport::DispatchServerCallPacketInSubtree(this, Connection, Data);
}

void MWorldServer::HandleBackendPacket(uint8 PacketType, const TByteArray& Data, const char* ServerName)
{
    if (PacketType == static_cast<uint8>(EServerMessageType::MT_FunctionResponse))
    {
        if (!HandleServerCallResponse(Data))
        {
            LOG_WARN("World failed to handle backend function response from %s", ServerName ? ServerName : "backend");
        }
        return;
    }

    LOG_WARN("World received unsupported backend packet from %s: type=%u",
             ServerName ? ServerName : "backend",
             static_cast<unsigned>(PacketType));
}

