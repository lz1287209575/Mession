#include "Servers/World/WorldServer.h"
#include "Servers/App/ServerRpcSupport.h"
#include "Common/Net/Rpc/RpcServerCall.h"

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

    LoginRpc->SetConnection(LoginServerConn);
    MgoRpc->SetConnection(MgoServerConn);
    SceneRpc->SetConnection(SceneServerConn);
    RouterRpc->SetConnection(RouterServerConn);
    PlayerService->Initialize(&OnlinePlayers, LoginRpc, MgoRpc, SceneRpc, RouterRpc);

    return true;
}

void MWorldServer::Tick()
{
    for (const auto& [PlayerId, Session] : OnlinePlayers)
    {
        (void)PlayerId;
        ForEachObjectInSubtree(Session, [this](MObject* Object)
        {
            (void)PersistenceSubsystem.EnqueueIfDirty(Object);
        });
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
    LoginServerConn.reset();
    SceneServerConn.reset();
    RouterServerConn.reset();
    MgoServerConn.reset();
}

void MWorldServer::OnRunStarted()
{
    LOG_INFO("World skeleton running on port %u", static_cast<unsigned>(Config.ListenPort));
}

void MWorldServer::HandlePeerPacket(uint64 /*ConnectionId*/, const TSharedPtr<INetConnection>& Connection, const TByteArray& Data)
{
    (void)MServerRpcSupport::DispatchServerCallPacket(PlayerService, Connection, Data);
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
