#include "WorldServer.h"
#include "Servers/World/Avatar/PlayerAvatar.h"
#include "Common/Runtime/Config.h"
#include "Common/Net/ServerRpcRuntime.h"
#include "Common/Runtime/StringUtils.h"
#include "Common/Runtime/HexUtils.h"
#include "Common/Runtime/Time.h"
#include "Common/Runtime/Json.h"

namespace
{
const TMap<MString, const char*> WorldEnvMap = {
    {"port", "MESSION_WORLD_PORT"},
    {"router_addr", "MESSION_ROUTER_ADDR"},
    {"router_port", "MESSION_ROUTER_PORT"},
    {"login_addr", "MESSION_LOGIN_ADDR"},
    {"login_port", "MESSION_LOGIN_PORT"},
    {"zone_id", "MESSION_ZONE_ID"},
    {"debug_http_port", "MESSION_WORLD_DEBUG_HTTP_PORT"},
    {"mgo_persistence_enable", "MESSION_WORLD_MGO_PERSISTENCE_ENABLE"},
    {"owner_server_id", "MESSION_WORLD_OWNER_SERVER_ID"},
};

class MWorldPersistenceLogSink : public IPersistenceSink
{
public:
    bool Persist(const SPersistenceRecord& InRecord) override
    {
        LOG_DEBUG(
            "Persistence flush: object=%llu class=%s owner=%u req=%llu ver=%llu bytes=%llu",
            static_cast<unsigned long long>(InRecord.ObjectId),
            InRecord.ClassName.c_str(),
            static_cast<unsigned>(InRecord.OwnerServerId),
            static_cast<unsigned long long>(InRecord.RequestId),
            static_cast<unsigned long long>(InRecord.Version),
            static_cast<unsigned long long>(InRecord.SnapshotData.size()));
        return true;
    }
};

class MWorldMgoRpcSink : public IPersistenceSink
{
public:
    explicit MWorldMgoRpcSink(TSharedPtr<MServerConnection>* InMgoConn, MWorldServer* InOwnerServer)
        : MgoConn(InMgoConn)
        , OwnerServer(InOwnerServer)
    {
    }

    bool Persist(const SPersistenceRecord& InRecord) override
    {
        if (!MgoConn || !(*MgoConn) || !(*MgoConn)->IsConnected())
        {
            return false;
        }

        const MString SnapshotHex = Hex::BytesToHex(InRecord.SnapshotData);
        const bool bSent = MRpc::Call(
            *MgoConn,
            EServerType::Mgo,
            "Rpc_OnPersistSnapshot",
            InRecord.ObjectId,
            InRecord.ClassId,
            InRecord.OwnerServerId,
            InRecord.RequestId,
            InRecord.Version,
            InRecord.ClassName,
            SnapshotHex);
        if (!bSent)
        {
            LOG_WARN("World->Mgo persist RPC send failed: object=%llu owner=%u req=%llu ver=%llu class=%s",
                     static_cast<unsigned long long>(InRecord.ObjectId),
                     static_cast<unsigned>(InRecord.OwnerServerId),
                     static_cast<unsigned long long>(InRecord.RequestId),
                     static_cast<unsigned long long>(InRecord.Version),
                     InRecord.ClassName.c_str());
            return false;
        }
        if (OwnerServer)
        {
            OwnerServer->OnPersistRequestDispatched(InRecord.RequestId, InRecord.ObjectId, InRecord.Version);
        }
        return true;
    }

private:
    TSharedPtr<MServerConnection>* MgoConn = nullptr;
    MWorldServer* OwnerServer = nullptr;
};
}

MWorldServer::MWorldServer()
{
    AddToRoot();
    ReplicationDriver = new MReplicationDriver();
    PersistenceSubsystem.SetSink(TUniquePtr<IPersistenceSink>(new MWorldPersistenceLogSink()));
}

bool MWorldServer::LoadConfig(const MString& ConfigPath)
{
    TMap<MString, MString> Vars;
    if (!ConfigPath.empty())
    {
        MConfig::LoadFromFile(ConfigPath, Vars);
    }
    MConfig::ApplyEnvOverrides(Vars, WorldEnvMap);
    Config.ListenPort = MConfig::GetU16(Vars, "port", Config.ListenPort);
    Config.RouterServerAddr = MConfig::GetStr(Vars, "router_addr", Config.RouterServerAddr);
    Config.RouterServerPort = MConfig::GetU16(Vars, "router_port", Config.RouterServerPort);
    Config.LoginServerAddr = MConfig::GetStr(Vars, "login_addr", Config.LoginServerAddr);
    Config.LoginServerPort = MConfig::GetU16(Vars, "login_port", Config.LoginServerPort);
    Config.ZoneId = MConfig::GetU16(Vars, "zone_id", Config.ZoneId);
    Config.MaxPlayers = MConfig::GetU32(Vars, "max_players", Config.MaxPlayers);
    Config.ServerName = MConfig::GetStr(Vars, "server_name", Config.ServerName);
    Config.DebugHttpPort = MConfig::GetU16(Vars, "debug_http_port", Config.DebugHttpPort);
    Config.EnableMgoPersistence = MConfig::GetBool(Vars, "mgo_persistence_enable", Config.EnableMgoPersistence);
    Config.OwnerServerId = MConfig::GetU32(Vars, "owner_server_id", Config.OwnerServerId);
    if (Config.OwnerServerId == 0)
    {
        Config.OwnerServerId = 3;
    }
    return true;
}

bool MWorldServer::Init(int InPort)
{
    if (InPort > 0)
    {
        Config.ListenPort = static_cast<uint16>(InPort);
    }
    MServerConnection::SetLocalInfo(Config.OwnerServerId, EServerType::World, Config.ServerName);
    PersistenceSubsystem.SetOwnerServerId(Config.OwnerServerId);

    bRunning = true;

    MLogger::LogStartupBanner("WorldServer", Config.ListenPort, 0);

    if (!Config.EnableMgoPersistence)
    {
        PersistenceSubsystem.SetSink(TUniquePtr<IPersistenceSink>(new MWorldPersistenceLogSink()));
        LOG_INFO("World persistence sink: Log");
    }

    
    SServerConnectionConfig RouterConfig(100, EServerType::Router, "Router01", Config.RouterServerAddr, Config.RouterServerPort);
    RouterServerConn = BackendConnectionManager.AddServer(RouterConfig);
    RouterServerConn->SetOnAuthenticated([this](auto, const SServerInfo& Info) {
        LOG_INFO("Router server authenticated: %s", Info.ServerName.c_str());
        SendRouterRegister();
        QueryLoginServerRoute();
        if (Config.EnableMgoPersistence)
        {
            QueryMgoServerRoute();
        }
    });
    RouterServerConn->SetOnMessage([this](auto, uint8 PacketType, const TByteArray& Data) {
        HandleRouterServerPacket(PacketType, Data);
    });
    RouterServerConn->Connect();

    SServerConnectionConfig LoginConfig(2, EServerType::Login, "Login01", "", 0);
    LoginServerConn = BackendConnectionManager.AddServer(LoginConfig);
    SServerConnectionConfig MgoConfig(6, EServerType::Mgo, "Mgo01", "", 0);
    MgoServerConn = BackendConnectionManager.AddServer(MgoConfig);

    // 启动调试 HTTP 服务器（仅当配置端口 > 0 时）
    if (Config.DebugHttpPort > 0)
    {
        DebugServer = TUniquePtr<MHttpDebugServer>(new MHttpDebugServer(
            Config.DebugHttpPort,
            [this]() { return BuildDebugStatusJson(); }));
        if (!DebugServer->Start())
        {
            LOG_ERROR("World debug HTTP failed to start on port %u", static_cast<unsigned>(Config.DebugHttpPort));
            DebugServer.reset();
        }
    }
    LoginServerConn->SetOnAuthenticated([](auto, const SServerInfo& Info) {
        LOG_INFO("Login server authenticated: %s", Info.ServerName.c_str());
    });
    LoginServerConn->SetOnMessage([this](auto, uint8 PacketType, const TByteArray& Data) {
        HandleLoginServerPacket(PacketType, Data);
    });

    if (MgoServerConn)
    {
        MgoServerConn->SetOnAuthenticated([this](auto, const SServerInfo& Info) {
            LOG_INFO("Mgo server authenticated: %s", Info.ServerName.c_str());
        });
        MgoServerConn->SetOnMessage([this](auto, uint8 PacketType, const TByteArray& Data) {
            if (PacketType == static_cast<uint8>(EServerMessageType::MT_RPC))
            {
                if (!TryInvokeServerRpc(this, Data, ERpcType::ServerToServer))
                {
                    LOG_WARN("WorldServer Mgo MT_RPC packet could not be handled via reflection");
                }
                return;
            }

            LOG_WARN("Unexpected non-RPC mgo server message type %u", static_cast<unsigned>(PacketType));
        });
        PersistenceSubsystem.SetSink(TUniquePtr<IPersistenceSink>(new MWorldMgoRpcSink(&MgoServerConn, this)));
        LOG_INFO("World persistence sink: Mgo RPC");
    }
    
    return true;
}

uint16 MWorldServer::GetListenPort() const
{
    return Config.ListenPort;
}

void MWorldServer::OnAccept(uint64 ConnId, TSharedPtr<INetConnection> Conn)
{
    Conn->SetNonBlocking(true);
    SBackendPeer Peer;
    Peer.Connection = Conn;
    BackendConnections[ConnId] = Peer;
    LOG_INFO("New connection (connection_id=%llu)", (unsigned long long)ConnId);
    EventLoop.RegisterConnection(ConnId, Conn,
        [this](uint64 Id, const TByteArray& Payload)
        {
            HandlePacket(Id, Payload);
        },
        [this](uint64 Id)
        {
            TVector<uint64> PlayerIdsToRemove;
            for (const auto& [PlayerId, Avatar] : Players)
            {
                MPlayerSession* Player = Avatar ? Avatar->GetPlayerSession() : nullptr;
                if (Player && Player->GetGatewayConnectionId() == Id)
                {
                    PlayerIdsToRemove.push_back(PlayerId);
                }
            }
            for (uint64 PlayerId : PlayerIdsToRemove)
            {
                RemovePlayer(PlayerId);
            }
            LOG_INFO("Connection disconnected: %llu", (unsigned long long)Id);
            BackendConnections.erase(Id);
        });
}

void MWorldServer::ShutdownConnections()
{
    for (auto& [Id, Peer] : BackendConnections)
    {
        if (Peer.Connection)
        {
            Peer.Connection->Close();
        }
    }
    BackendConnections.clear();
    BackendConnectionManager.DisconnectAll();
    RouterServerConn.reset();
    LoginServerConn.reset();
    MgoServerConn.reset();
    PendingSessionValidations.clear();
    TVector<uint64> PlayerIds;
    PlayerIds.reserve(Players.size());
    for (const auto& [PlayerId, Avatar] : Players)
    {
        (void)Avatar;
        PlayerIds.push_back(PlayerId);
    }
    for (uint64 PlayerId : PlayerIds)
    {
        RemovePlayer(PlayerId);
    }
    Players.clear();
    delete ReplicationDriver;
    ReplicationDriver = nullptr;
    if (DebugServer)
    {
        DebugServer->Stop();
        DebugServer.reset();
    }
    LOG_INFO("World server shutdown complete");
}

void MWorldServer::OnRunStarted()
{
    LOG_INFO("World server running...");
}

void MWorldServer::Tick()
{
    if (!bRunning)
    {
        return;
    }
    TickBackends();
}

void MWorldServer::TickBackends()
{
    // 统一驱动所有后端连接
    BackendConnectionManager.Tick(DEFAULT_TICK_RATE);

    if (RouterServerConn)
    {
        RouterServerConn->Tick(DEFAULT_TICK_RATE);
    }

    LoginRouteQueryTimer += DEFAULT_TICK_RATE;
    if (RouterServerConn && RouterServerConn->IsConnected() && LoginRouteQueryTimer >= 1.0f)
    {
        LoginRouteQueryTimer = 0.0f;
        if (!LoginServerConn || !LoginServerConn->IsConnected())
        {
            QueryLoginServerRoute();
        }
    }

    MgoRouteQueryTimer += DEFAULT_TICK_RATE;
    if (RouterServerConn && RouterServerConn->IsConnected() && MgoRouteQueryTimer >= 1.0f)
    {
        MgoRouteQueryTimer = 0.0f;
        if (Config.EnableMgoPersistence && (!MgoServerConn || !MgoServerConn->IsConnected()))
        {
            QueryMgoServerRoute();
        }
    }

    LoadReportTimer += DEFAULT_TICK_RATE;
    if (RouterServerConn && RouterServerConn->IsConnected() && LoadReportTimer >= 5.0f)
    {
        LoadReportTimer = 0.0f;
        SendLoadReport();
    }

    if (LoginServerConn)
    {
        LoginServerConn->Tick(DEFAULT_TICK_RATE);
    }

    for (auto& [Id, Peer] : BackendConnections)
    {
        if (Peer.Connection)
        {
            Peer.Connection->FlushSendBuffer();
        }
    }

    UpdateGameLogic(DEFAULT_TICK_RATE);

    if (ReplicationDriver)
    {
        ReplicationDriver->Tick(DEFAULT_TICK_RATE);
    }
    PersistenceSubsystem.Flush(64);

    const double Now = MTime::GetTimeSeconds();
    for (auto It = PendingMgoPersists.begin(); It != PendingMgoPersists.end();)
    {
        const double Elapsed = Now - It->second.DispatchTime;
        if (Elapsed < 10.0)
        {
            ++It;
            continue;
        }

        ++PersistAckTimeoutCount;
        LOG_WARN("Mgo persist ACK timeout: request=%llu object=%llu version=%llu elapsed=%.2fs",
                 static_cast<unsigned long long>(It->second.RequestId),
                 static_cast<unsigned long long>(It->second.ObjectId),
                 static_cast<unsigned long long>(It->second.Version),
                 Elapsed);
        It = PendingMgoPersists.erase(It);
    }
}

MString MWorldServer::BuildDebugStatusJson() const
{
    const SConnectionManagerStats Stats = BackendConnectionManager.GetStats();
    const size_t PlayerCount = Players.size();

    MJsonWriter W = MJsonWriter::Object();
    W.Key("server"); W.Value("World");
    W.Key("players"); W.Value(static_cast<uint64>(PlayerCount));
    W.Key("backendTotal"); W.Value(static_cast<uint64>(Stats.Total));
    W.Key("backendActive"); W.Value(static_cast<uint64>(Stats.Active));
    W.Key("bytesSent"); W.Value(static_cast<uint64>(Stats.BytesSent));
    W.Key("bytesReceived"); W.Value(static_cast<uint64>(Stats.BytesReceived));
    W.Key("reconnectAttempts"); W.Value(static_cast<uint64>(Stats.ReconnectAttempts));
    const TVector<MString> RpcFunctions = GetGeneratedRpcFunctionNames(EServerType::World);
    W.Key("rpcManifestCount"); W.Value(static_cast<uint64>(RpcFunctions.size()));
    W.Key("mgoPersistenceEnabled"); W.Value(Config.EnableMgoPersistence);
    W.Key("ownerServerId"); W.Value(static_cast<uint64>(Config.OwnerServerId));
    W.Key("mgoConnected"); W.Value(MgoServerConn && MgoServerConn->IsConnected());
    W.Key("persistencePending"); W.Value(PersistenceSubsystem.GetPendingCount());
    W.Key("persistenceEnqueued"); W.Value(PersistenceSubsystem.GetEnqueuedCount());
    W.Key("persistenceFlushed"); W.Value(PersistenceSubsystem.GetFlushedCount());
    W.Key("persistenceMerged"); W.Value(PersistenceSubsystem.GetMergedCount());
    W.Key("persistenceRetryBlocked"); W.Value(PersistenceSubsystem.GetRetryBlockedCount());
    W.Key("persistAckPending"); W.Value(static_cast<uint64>(PendingMgoPersists.size()));
    W.Key("persistAckSuccess"); W.Value(PersistAckSuccessCount);
    W.Key("persistAckFailed"); W.Value(PersistAckFailedCount);
    W.Key("persistAckTimeout"); W.Value(PersistAckTimeoutCount);
    W.Key("persistAckUnmatched"); W.Value(PersistAckUnmatchedCount);
    W.Key("rpcFunctions");
    W.BeginArray();
    for (const MString& Name : RpcFunctions)
    {
        W.Value(Name);
    }
    W.EndArray();
    W.Key("unsupportedRpc");
    W.BeginArray();
    for (const SGeneratedRpcUnsupportedStat& Stat : GetGeneratedRpcUnsupportedStats(EServerType::World))
    {
        W.BeginObject();
        W.Key("function"); W.Value(Stat.FunctionName);
        W.Key("count"); W.Value(Stat.Count);
        W.EndObject();
    }
    W.EndArray();
    W.EndObject();
    return W.ToString();
}

void MWorldServer::HandlePacket(uint64 ConnectionId, const TByteArray& Data)
{
    if (Data.empty())
    {
        return;
    }

    auto PeerIt = BackendConnections.find(ConnectionId);
    if (PeerIt == BackendConnections.end())
    {
        return;
    }

    const uint8 PacketType = Data[0];
    const TByteArray Payload(Data.begin() + 1, Data.end());

    if (PacketType == static_cast<uint8>(EServerMessageType::MT_RPC))
    {
        const uint16 HandshakeFunctionId = MGET_STABLE_RPC_FUNCTION_ID("MWorldServer", "Rpc_OnServerHandshake");
        const bool bAuthenticated = PeerIt->second.bAuthenticated;
        if (!bAuthenticated && PeekServerRpcFunctionId(Payload) != HandshakeFunctionId)
        {
            LOG_WARN("WorldServer rejecting non-handshake MT_RPC from unauthenticated connection %llu",
                     static_cast<unsigned long long>(ConnectionId));
            return;
        }

        if (!TryInvokeServerRpc(this, ConnectionId, Payload, ERpcType::ServerToServer))
        {
            LOG_WARN("WorldServer backend MT_RPC packet could not be handled via reflection");
        }
        return;
    }

    LOG_WARN("WorldServer received unexpected non-RPC backend message type %u from connection %llu",
             static_cast<unsigned>(PacketType),
             static_cast<unsigned long long>(ConnectionId));
}
