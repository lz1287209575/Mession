#include "WorldServer.h"
#include "Build/Generated/MLoginService.mgenerated.h"
#include "Build/Generated/MMgoService.mgenerated.h"
#include "Common/Config.h"
#include "Common/ServerRpcRuntime.h"
#include "Common/StringUtils.h"
#include "Gameplay/InventoryMember.h"
#include "Messages/NetMessages.h"
#include "Core/Json.h"
#include <sstream>

namespace
{
const TMap<FString, const char*> WorldEnvMap = {
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

class MGatewayClientTunnelConnection : public INetConnection
{
public:
    MGatewayClientTunnelConnection(
        TFunction<bool(const void*, uint32)> InSendCallback,
        TFunction<bool()> InIsConnectedCallback)
        : SendCallback(InSendCallback)
        , IsConnectedCallback(InIsConnectedCallback)
    {
    }

    bool Send(const void* Data, uint32 Size) override
    {
        if (!Data || Size == 0 || !SendCallback)
        {
            return false;
        }

        return SendCallback(Data, Size);
    }

    bool Receive(void* /*Buffer*/, uint32 /*BufferSize*/, uint32& OutBytesReceived) override
    {
        OutBytesReceived = 0;
        return false;
    }

    uint64 GetPlayerId() const override
    {
        return PlayerId;
    }

    void SetPlayerId(uint64 Id) override
    {
        PlayerId = Id;
    }

    bool ReceivePacket(TArray& /*OutPayload*/) override
    {
        return false;
    }

    void Close() override
    {
    }

    bool IsConnected() const override
    {
        return IsConnectedCallback ? IsConnectedCallback() : false;
    }

    TSocketFd GetSocketFd() const override
    {
        return INVALID_SOCKET_FD;
    }

    void SetNonBlocking(bool /*bNonBlocking*/) override
    {
    }

private:
    uint64 PlayerId = 0;
    TFunction<bool(const void*, uint32)> SendCallback;
    TFunction<bool()> IsConnectedCallback;
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

FString BytesToHex(const TArray& InData)
{
    static const char* Digits = "0123456789ABCDEF";
    FString Out;
    Out.reserve(InData.size() * 2);
    for (uint8 Byte : InData)
    {
        Out.push_back(Digits[(Byte >> 4) & 0x0F]);
        Out.push_back(Digits[Byte & 0x0F]);
    }
    return Out;
}

bool TryDecodeHex(const FString& InHex, TArray& OutBytes)
{
    OutBytes.clear();
    if (InHex.empty())
    {
        return true;
    }
    if ((InHex.size() % 2) != 0)
    {
        return false;
    }

    auto HexNibble = [](char Ch) -> int32
    {
        if (Ch >= '0' && Ch <= '9')
        {
            return static_cast<int32>(Ch - '0');
        }
        if (Ch >= 'A' && Ch <= 'F')
        {
            return 10 + static_cast<int32>(Ch - 'A');
        }
        if (Ch >= 'a' && Ch <= 'f')
        {
            return 10 + static_cast<int32>(Ch - 'a');
        }
        return -1;
    };

    OutBytes.reserve(InHex.size() / 2);
    for (size_t Index = 0; Index < InHex.size(); Index += 2)
    {
        const int32 Hi = HexNibble(InHex[Index]);
        const int32 Lo = HexNibble(InHex[Index + 1]);
        if (Hi < 0 || Lo < 0)
        {
            OutBytes.clear();
            return false;
        }
        OutBytes.push_back(static_cast<uint8>((Hi << 4) | Lo));
    }
    return true;
}

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

        const FString SnapshotHex = BytesToHex(InRecord.SnapshotData);
        const bool bSent = MRpc::MMgoService::Rpc_OnPersistSnapshot(
            *MgoConn,
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
    ReplicationDriver = new MReplicationDriver();
    PersistenceSubsystem.SetSink(TUniquePtr<IPersistenceSink>(new MWorldPersistenceLogSink()));
}

bool MWorldServer::LoadConfig(const FString& ConfigPath)
{
    TMap<FString, FString> Vars;
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

    // 初始化服务器消息分发器
    InitBackendMessageHandlers();
    InitRouterMessageHandlers();
    InitLoginMessageHandlers();

    MWorldService::BindHandlers(this);

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
    RouterServerConn->SetOnMessage([this](auto, uint8 Type, const TArray& Data) {
        HandleRouterServerMessage(Type, Data);
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
    LoginServerConn->SetOnMessage([this](auto, uint8 Type, const TArray& Data) {
        HandleLoginServerMessage(Type, Data);
    });

    if (MgoServerConn)
    {
        MgoServerConn->SetOnAuthenticated([this](auto, const SServerInfo& Info) {
            LOG_INFO("Mgo server authenticated: %s", Info.ServerName.c_str());
        });
        MgoServerConn->SetOnMessage([this](auto, uint8 Type, const TArray& Data) {
            if (Type == static_cast<uint8>(EServerMessageType::MT_RPC))
            {
                LOG_DEBUG("World received MT_RPC from Mgo (%llu bytes)", static_cast<unsigned long long>(Data.size()));
            }
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
        [this](uint64 Id, const TArray& Payload)
        {
            HandlePacket(Id, Payload);
        },
        [this](uint64 Id)
        {
            TVector<uint64> PlayerIdsToRemove;
            for (const auto& [PlayerId, Player] : Players)
            {
                if (Player.GatewayConnectionId == Id)
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

FString MWorldServer::BuildDebugStatusJson() const
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
    const TVector<FString> RpcFunctions = GetGeneratedRpcFunctionNames(EServerType::World);
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
    for (const FString& Name : RpcFunctions)
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

void MWorldServer::HandlePacket(uint64 ConnectionId, const TArray& Data)
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

    const uint8 MsgType = Data[0];
    const TArray Payload(Data.begin() + 1, Data.end());

    if (MsgType == static_cast<uint8>(EServerMessageType::MT_RPC))
    {
        if (!TryInvokeServerRpc(&WorldService, Payload, ERpcType::ServerToServer))
        {
            LOG_WARN("WorldServer backend MT_RPC packet could not be handled via reflection");
        }
        return;
    }

    BackendMessageDispatcher.Dispatch(ConnectionId, MsgType, Payload);
}

void MWorldServer::HandleGameplayPacket(uint64 PlayerId, const TArray& Data)
{
    if (Data.empty())
    {
        return;
    }

    const EClientMessageType MsgType = (EClientMessageType)Data[0];

    switch (MsgType)
    {
        case EClientMessageType::MT_PlayerMove:
        {
            TArray Payload(Data.begin() + 1, Data.end());
            SPlayerMovePayload MovePayload;
            auto ParseResult = ParsePayload(Payload, MovePayload, "MT_PlayerMove");
            if (!ParseResult.IsOk())
            {
                LOG_WARN("ParsePayload failed: %s", ParseResult.GetError().c_str());
                return;
            }

            auto* Player = GetPlayerById(PlayerId);
            if (!Player || !Player->Avatar)
            {
                return;
            }

            const SVector NewPos(MovePayload.X, MovePayload.Y, MovePayload.Z);
            Player->Avatar->SetLocation(NewPos);

            const SPlayerSceneStateMessage Message{
                Player->PlayerId,
                static_cast<uint16>(Player->CurrentSceneId),
                NewPos.X,
                NewPos.Y,
                NewPos.Z
            };
            BroadcastToScenes((uint8)EServerMessageType::MT_PlayerDataSync, BuildPayload(Message));
            
            LOG_DEBUG("Player %llu moved to (%.2f, %.2f, %.2f)",
                     (unsigned long long)Player->PlayerId, NewPos.X, NewPos.Y, NewPos.Z);
            break;
        }
        case EClientMessageType::MT_Chat:
        {
            TArray Payload(Data.begin() + 1, Data.end());
            SClientChatPayload ChatPayload;
            auto ParseResult = ParsePayload(Payload, ChatPayload, "MT_Chat");
            if (!ParseResult.IsOk())
            {
                LOG_WARN("ParsePayload failed: %s", ParseResult.GetError().c_str());
                return;
            }

            if (ChatPayload.Message.empty())
            {
                LOG_WARN("Ignoring empty MT_Chat from player %llu", (unsigned long long)PlayerId);
                return;
            }

            auto SendChatToPlayer = [this](uint64 InTargetPlayerId, uint64 InFromPlayerId, const FString& InMessage)
            {
                auto TargetIt = Players.find(InTargetPlayerId);
                if (TargetIt == Players.end() || !TargetIt->second.bOnline || TargetIt->second.GatewayConnectionId == 0)
                {
                    return;
                }
                const SChatMessage OutgoingChat{InFromPlayerId, InMessage};
                const TArray ChatPayloadBytes = BuildPayload(OutgoingChat);
                TArray ChatPacket;
                ChatPacket.reserve(1 + ChatPayloadBytes.size());
                ChatPacket.push_back(static_cast<uint8>(EClientMessageType::MT_Chat));
                ChatPacket.insert(ChatPacket.end(), ChatPayloadBytes.begin(), ChatPayloadBytes.end());
                SendServerMessage(
                    TargetIt->second.GatewayConnectionId,
                    EServerMessageType::MT_PlayerClientSync,
                    SPlayerClientSyncMessage{InTargetPlayerId, ChatPacket});
            };

            if (ChatPayload.Message.rfind("/bag", 0) == 0)
            {
                SPlayer* Player = GetPlayerById(PlayerId);
                if (!Player || !Player->Avatar)
                {
                    return;
                }

                MInventoryMember* Inventory = Player->Avatar->GetRequiredMember<MInventoryMember>();
                if (!Inventory)
                {
                    SendChatToPlayer(PlayerId, 0, "[bag] inventory member missing");
                    return;
                }

                std::istringstream SS(ChatPayload.Message);
                FString Cmd;
                FString Action;
                SS >> Cmd >> Action;

                if (Action == "add")
                {
                    uint32 ItemId = 0;
                    SS >> ItemId;
                    if (ItemId == 0)
                    {
                        SendChatToPlayer(PlayerId, 0, "[bag] usage: /bag add <item_id>");
                        return;
                    }
                    const bool bAdded = Inventory->AddItem(ItemId);
                    if (!bAdded)
                    {
                        SendChatToPlayer(PlayerId, 0, "[bag] add failed: bag full or invalid item");
                        return;
                    }
                    PersistenceSubsystem.EnqueueIfDirty(Inventory, Inventory->GetClass());
                    (void)SendInventoryPullToPlayer(PlayerId);
                    SendChatToPlayer(
                        PlayerId,
                        0,
                        "[bag] add ok: item=" + MString::ToString(ItemId) +
                        " count=" + MString::ToString(static_cast<uint64>(Inventory->GetItemCount(ItemId))));
                    return;
                }
                if (Action == "del")
                {
                    uint32 ItemId = 0;
                    SS >> ItemId;
                    if (ItemId == 0)
                    {
                        SendChatToPlayer(PlayerId, 0, "[bag] usage: /bag del <item_id>");
                        return;
                    }
                    const bool bRemoved = Inventory->RemoveItem(ItemId);
                    if (bRemoved)
                    {
                        PersistenceSubsystem.EnqueueIfDirty(Inventory, Inventory->GetClass());
                        (void)SendInventoryPullToPlayer(PlayerId);
                    }
                    SendChatToPlayer(PlayerId, 0, bRemoved
                        ? ("[bag] del ok: item=" + MString::ToString(ItemId))
                        : ("[bag] del miss: item=" + MString::ToString(ItemId)));
                    return;
                }
                if (Action == "gold")
                {
                    int32 Delta = 0;
                    SS >> Delta;
                    if (Delta >= 0)
                    {
                        Inventory->AddGold(Delta);
                    }
                    else
                    {
                        (void)Inventory->SpendGold(-Delta);
                    }
                    PersistenceSubsystem.EnqueueIfDirty(Inventory, Inventory->GetClass());
                    (void)SendInventoryPullToPlayer(PlayerId);
                    SendChatToPlayer(PlayerId, 0, "[bag] gold=" + MString::ToString(Inventory->GetGold()));
                    return;
                }
                if (Action == "show")
                {
                    if (!SendInventoryPullToPlayer(PlayerId))
                    {
                        SendChatToPlayer(PlayerId, 0, "[bag] pull failed");
                    }
                    else
                    {
                        SendChatToPlayer(PlayerId, 0, "[bag] " + Inventory->BuildSummary());
                    }
                    return;
                }

                SendChatToPlayer(PlayerId, 0, "[bag] commands: /bag add|del|gold|show");
                return;
            }

            const SChatMessage OutgoingChat{PlayerId, ChatPayload.Message};
            const TArray ChatPayloadBytes = BuildPayload(OutgoingChat);
            TArray ChatPacket;
            ChatPacket.reserve(1 + ChatPayloadBytes.size());
            ChatPacket.push_back(static_cast<uint8>(EClientMessageType::MT_Chat));
            ChatPacket.insert(ChatPacket.end(), ChatPayloadBytes.begin(), ChatPayloadBytes.end());

            uint32 DeliveredCount = 0;
            for (const auto& [TargetPlayerId, TargetPlayer] : Players)
            {
                if (!TargetPlayer.bOnline || TargetPlayer.GatewayConnectionId == 0)
                {
                    continue;
                }

                const bool bSent = SendServerMessage(
                    TargetPlayer.GatewayConnectionId,
                    EServerMessageType::MT_PlayerClientSync,
                    SPlayerClientSyncMessage{TargetPlayerId, ChatPacket});
                if (bSent)
                {
                    ++DeliveredCount;
                }
            }

            LOG_INFO("Player %llu chat broadcast delivered to %u player(s): %s",
                     (unsigned long long)PlayerId,
                     static_cast<unsigned>(DeliveredCount),
                     ChatPayload.Message.c_str());
            break;
        }
        default:
            LOG_DEBUG("Unknown message type: %d", (int)MsgType);
            break;
    }
}

bool MWorldServer::SendClientFunctionPacketToPlayer(uint64 PlayerId, const TArray& Packet)
{
    auto TargetIt = Players.find(PlayerId);
    if (TargetIt == Players.end() || !TargetIt->second.bOnline || TargetIt->second.GatewayConnectionId == 0)
    {
        return false;
    }

    return SendServerMessage(
        TargetIt->second.GatewayConnectionId,
        EServerMessageType::MT_PlayerClientSync,
        SPlayerClientSyncMessage{PlayerId, Packet});
}

bool MWorldServer::SendInventoryPullToPlayer(uint64 PlayerId)
{
    SPlayer* Player = GetPlayerById(PlayerId);
    if (!Player || !Player->Avatar)
    {
        return false;
    }

    MInventoryMember* Inventory = Player->Avatar->GetRequiredMember<MInventoryMember>();
    if (!Inventory)
    {
        return false;
    }

    SClientInventoryPullPayload Payload;
    Payload.PlayerId = PlayerId;
    Payload.Gold = Inventory->GetGold();
    Payload.MaxSlots = Inventory->GetMaxSlots();
    Payload.Items.reserve(Inventory->GetItems().size());
    for (const SInventoryItem& Item : Inventory->GetItems())
    {
        Payload.Items.push_back(SClientInventoryItemPayload{
            Item.InstanceId,
            Item.ItemId,
            Item.Count,
            Item.bBound,
            Item.ExpireAtUnixSeconds,
            Item.Flags,
        });
    }

    TArray Packet;
    if (!BuildClientFunctionCallPacketForPayload(MClientDownlink::Id_OnInventoryPull(), Payload, Packet))
    {
        return false;
    }

    return SendClientFunctionPacketToPlayer(PlayerId, Packet);
}

void MWorldServer::AddPlayer(uint64 PlayerId, const FString& Name, uint64 GatewayConnectionId)
{
    if (Players.find(PlayerId) != Players.end())
    {
        return;
    }

    SPlayer Player;
    Player.PlayerId = PlayerId;
    Player.Name = Name;
    Player.GatewayConnectionId = GatewayConnectionId;
    Player.CurrentSceneId = 1;
    Player.bOnline = true;
    
    // 创建角色
    MPlayerAvatar* Avatar = new MPlayerAvatar();
    Avatar->SetOwnerPlayerId(PlayerId);
    Avatar->SetDisplayName(Name);
    Avatar->SetLocation(SVector(-1040, 0, 90));

    Player.Avatar = Avatar;
    Players[PlayerId] = Player;

    TSharedPtr<INetConnection> ReplicationConnection = MakeShared<MGatewayClientTunnelConnection>(
        [this, GatewayConnectionId, PlayerId](const void* Data, uint32 Size) -> bool
        {
            TArray PacketBytes;
            const uint8* ByteData = static_cast<const uint8*>(Data);
            PacketBytes.insert(PacketBytes.end(), ByteData, ByteData + Size);
            const bool bOk = SendServerMessage(
                GatewayConnectionId,
                EServerMessageType::MT_PlayerClientSync,
                SPlayerClientSyncMessage{PlayerId, PacketBytes});
            if (!bOk)
            {
                LOG_WARN("Tunnel send to gateway conn %llu for player %llu failed",
                         (unsigned long long)GatewayConnectionId, (unsigned long long)PlayerId);
            }
            return bOk;
        },
        [this, GatewayConnectionId]() -> bool
        {
            auto GatewayIt = BackendConnections.find(GatewayConnectionId);
            return GatewayIt != BackendConnections.end() &&
                GatewayIt->second.bAuthenticated &&
                GatewayIt->second.ServerType == EServerType::Gateway &&
                GatewayIt->second.Connection &&
                GatewayIt->second.Connection->IsConnected();
        });
    ReplicationConnection->SetPlayerId(PlayerId);

    // 注册到复制系统
    ReplicationDriver->RegisterActor(Avatar);
    ReplicationDriver->AddConnection(PlayerId, ReplicationConnection);
    ReplicationDriver->AddRelevantActor(PlayerId, Avatar->GetObjectId());
    // Also send the initial actor create to the owning player so a single UE client
    // can verify the world-sync path without requiring a second connected client.
    ReplicationDriver->BroadcastActorCreate(Avatar);

    const SPlayerSceneStateMessage Message{
        Player.PlayerId,
        static_cast<uint16>(Player.CurrentSceneId),
        Player.Avatar->GetLocation().X,
        Player.Avatar->GetLocation().Y,
        Player.Avatar->GetLocation().Z
    };
    BroadcastToScenes((uint8)EServerMessageType::MT_PlayerSwitchServer, BuildPayload(Message));
    
    LOG_INFO("Player %s (id=%llu) added to world", 
             Name.c_str(), (unsigned long long)PlayerId);
}

void MWorldServer::RemovePlayer(uint64 PlayerId)
{
    auto It = Players.find(PlayerId);
    if (It == Players.end())
    {
        return;
    }
    
    SPlayer& Player = It->second;
    
    // 移除复制
    ReplicationDriver->RemoveConnection(Player.PlayerId);
    if (Player.Avatar)
    {
        ReplicationDriver->BroadcastActorDestroy(Player.Avatar->GetObjectId());
        delete Player.Avatar;
        Player.Avatar = nullptr;
    }
    BroadcastToScenes(
        (uint8)EServerMessageType::MT_PlayerLogout,
        BuildPayload(SPlayerSceneLeaveMessage{Player.PlayerId, static_cast<uint16>(Player.CurrentSceneId)}));
    
    Players.erase(It);
    
    LOG_INFO("Player %llu removed from world", (unsigned long long)PlayerId);
}

SPlayer* MWorldServer::GetPlayerById(uint64 PlayerId)
{
    auto It = Players.find(PlayerId);
    return (It != Players.end()) ? &It->second : nullptr;
}

void MWorldServer::UpdateGameLogic(float DeltaTime)
{
    // 更新所有玩家角色
    for (auto& [PlayerId, Player] : Players)
    {
        if (Player.Avatar)
        {
            static_cast<MActor*>(Player.Avatar)->Tick(DeltaTime);
            PersistenceSubsystem.EnqueueIfDirty(Player.Avatar, Player.Avatar->GetClass());
            for (const TUniquePtr<MAvatarMember>& Member : Player.Avatar->GetMembers())
            {
                if (!Member)
                {
                    continue;
                }
                PersistenceSubsystem.EnqueueIfDirty(Member.get(), Member->GetClass());
            }
        }
    }
}

bool MWorldServer::SendServerMessage(uint64 ConnectionId, uint8 Type, const TArray& Payload)
{
    auto It = BackendConnections.find(ConnectionId);
    if (It == BackendConnections.end() || !It->second.Connection)
    {
        return false;
    }

    TArray Packet;
    Packet.reserve(1 + Payload.size());
    Packet.push_back(Type);
    Packet.insert(Packet.end(), Payload.begin(), Payload.end());
    return It->second.Connection->Send(Packet.data(), Packet.size());
}

uint64 MWorldServer::FindAuthenticatedBackendConnectionId(EServerType ServerType) const
{
    for (const auto& [ConnectionId, Peer] : BackendConnections)
    {
        if (Peer.bAuthenticated && Peer.ServerType == ServerType && Peer.Connection)
        {
            return ConnectionId;
        }
    }

    return 0;
}

void MWorldServer::BroadcastToScenes(uint8 Type, const TArray& Payload)
{
    for (auto& [ConnectionId, Peer] : BackendConnections)
    {
        if (!Peer.bAuthenticated || Peer.ServerType != EServerType::Scene || !Peer.Connection)
        {
            continue;
        }

        SendServerMessage(ConnectionId, Type, Payload);
    }
}

void MWorldServer::HandleLoginServerMessage(uint8 Type, const TArray& Data)
{
    // 新的服务器间 RPC：Type 为 MT_RPC，Data 格式：
    // [FunctionId(2)][PayloadSize(4)][Payload...]
    if (Type == static_cast<uint8>(EServerMessageType::MT_RPC))
    {
        if (!TryInvokeServerRpc(&WorldService, Data, ERpcType::ServerToServer))
        {
            LOG_WARN("WorldServer MT_RPC packet could not be handled via reflection");
        }
        return;
    }

    // 兼容旧的基于消息类型的处理（如有）
    LoginMessageDispatcher.Dispatch(Type, Data);
}

void MWorldServer::RequestSessionValidation(uint64 GatewayConnectionId, uint64 PlayerId, uint32 SessionKey)
{
    if (!LoginServerConn || !LoginServerConn->IsConnected())
    {
        LOG_WARN("Login server unavailable, cannot validate session for player %llu",
                 (unsigned long long)PlayerId);
        return;
    }

    uint64 ValidationRequestId = NextSessionValidationId++;
    if (ValidationRequestId == 0)
    {
        ValidationRequestId = NextSessionValidationId++;
    }

    PendingSessionValidations[ValidationRequestId] = {ValidationRequestId, GatewayConnectionId, PlayerId, SessionKey};

    MRpc::TryRpcOrTypedLegacy(
        [&]()
        {
            return MRpc::MLoginService::Rpc_OnSessionValidateRequest(
                LoginServerConn,
                ValidationRequestId,
                PlayerId,
                SessionKey);
        },
        LoginServerConn,
        EServerMessageType::MT_SessionValidateRequest,
        SSessionValidateRequestMessage{ValidationRequestId, PlayerId, SessionKey});
}

void MWorldServer::HandleRouterServerMessage(uint8 Type, const TArray& Data)
{
    if (Type == static_cast<uint8>(EServerMessageType::MT_RPC))
    {
        if (!TryInvokeServerRpc(this, Data, ERpcType::ServerToServer) &&
            !TryInvokeServerRpc(&WorldService, Data, ERpcType::ServerToServer))
        {
            LOG_WARN("WorldServer router MT_RPC packet could not be handled via reflection");
        }
        return;
    }

    RouterMessageDispatcher.Dispatch(Type, Data);
}

void MWorldServer::InitBackendMessageHandlers()
{
    MREGISTER_SERVER_MESSAGE_HANDLER(
        BackendMessageDispatcher,
        EServerMessageType::MT_ServerHandshake,
        &MWorldServer::OnBackend_ServerHandshake,
        "MT_ServerHandshake");

    MREGISTER_SERVER_MESSAGE_HANDLER(
        BackendMessageDispatcher,
        EServerMessageType::MT_Heartbeat,
        &MWorldServer::OnBackend_Heartbeat,
        "MT_Heartbeat");

    MREGISTER_SERVER_MESSAGE_HANDLER(
        BackendMessageDispatcher,
        EServerMessageType::MT_PlayerLogin,
        &MWorldServer::OnBackend_PlayerLogin,
        "MT_PlayerLogin");

    MREGISTER_SERVER_MESSAGE_HANDLER(
        BackendMessageDispatcher,
        EServerMessageType::MT_PlayerLogout,
        &MWorldServer::OnBackend_PlayerLogout,
        "MT_PlayerLogout");

    MREGISTER_SERVER_MESSAGE_HANDLER(
        BackendMessageDispatcher,
        EServerMessageType::MT_PlayerClientSync,
        &MWorldServer::OnBackend_PlayerClientSync,
        "MT_PlayerClientSync");
}

void MWorldServer::InitRouterMessageHandlers()
{
    MREGISTER_SERVER_MESSAGE_HANDLER(
        RouterMessageDispatcher,
        EServerMessageType::MT_ServerRegisterAck,
        &MWorldServer::OnRouter_ServerRegisterAck,
        "MT_ServerRegisterAck");

    MREGISTER_SERVER_MESSAGE_HANDLER(
        RouterMessageDispatcher,
        EServerMessageType::MT_RouteResponse,
        &MWorldServer::OnRouter_RouteResponse,
        "MT_RouteResponse");
}

void MWorldServer::InitLoginMessageHandlers()
{
    MREGISTER_SERVER_MESSAGE_HANDLER(
        LoginMessageDispatcher,
        EServerMessageType::MT_SessionValidateResponse,
        &MWorldServer::OnLogin_SessionValidateResponseMessage,
        "MT_SessionValidateResponse");
}

void MWorldServer::OnBackend_ServerHandshake(uint64 ConnectionId, const SServerHandshakeMessage& Message)
{
    auto PeerIt = BackendConnections.find(ConnectionId);
    if (PeerIt == BackendConnections.end())
    {
        return;
    }

    SBackendPeer& Peer = PeerIt->second;
    Peer.ServerId = Message.ServerId;
    Peer.ServerType = Message.ServerType;
    Peer.ServerName = Message.ServerName;
    Peer.bAuthenticated = true;

    SendServerMessage(ConnectionId, EServerMessageType::MT_ServerHandshakeAck, SEmptyServerMessage{});
    LOG_INFO("%s authenticated as %d", Peer.ServerName.c_str(), (int)Peer.ServerType);
}

void MWorldServer::OnBackend_Heartbeat(uint64 ConnectionId, const SHeartbeatMessage& /*Message*/)
{
    SendServerMessage(ConnectionId, EServerMessageType::MT_HeartbeatAck, SEmptyServerMessage{});
}

void MWorldServer::OnBackend_PlayerLogin(uint64 ConnectionId, const SPlayerLoginResponseMessage& Message)
{
    auto PeerIt = BackendConnections.find(ConnectionId);
    if (PeerIt == BackendConnections.end())
    {
        return;
    }

    const SBackendPeer& Peer = PeerIt->second;
    if (!Peer.bAuthenticated || Peer.ServerType != EServerType::Gateway)
    {
        return;
    }

    RequestSessionValidation(ConnectionId, Message.PlayerId, Message.SessionKey);
}

void MWorldServer::OnBackend_PlayerLogout(uint64 ConnectionId, const SPlayerLogoutMessage& Message)
{
    auto PeerIt = BackendConnections.find(ConnectionId);
    if (PeerIt == BackendConnections.end())
    {
        return;
    }

    const SBackendPeer& Peer = PeerIt->second;
    if (!Peer.bAuthenticated || Peer.ServerType != EServerType::Gateway)
    {
        return;
    }

    RemovePlayer(Message.PlayerId);
}

void MWorldServer::OnBackend_PlayerClientSync(uint64 ConnectionId, const SPlayerClientSyncMessage& Message)
{
    auto PeerIt = BackendConnections.find(ConnectionId);
    if (PeerIt == BackendConnections.end())
    {
        return;
    }

    const SBackendPeer& Peer = PeerIt->second;
    if (!Peer.bAuthenticated || Peer.ServerType != EServerType::Gateway)
    {
        return;
    }

    HandleGameplayPacket(Message.PlayerId, Message.Data);
}

void MWorldServer::OnRouter_ServerRegisterAck(const SServerRegisterAckMessage& /*Message*/)
{
    LOG_INFO("World server registered to RouterServer");
}

void MWorldServer::Rpc_OnRouterServerRegisterAck(uint8 Result)
{
    OnRouter_ServerRegisterAck(SServerRegisterAckMessage{Result});
}

void MWorldServer::OnRouter_RouteResponse(const SRouteResponseMessage& Message)
{
    if (Message.PlayerId != 0 || !Message.bFound)
    {
        return;
    }

    if (Message.RequestedType == EServerType::Login)
    {
        ApplyLoginServerRoute(
            Message.ServerInfo.ServerId,
            Message.ServerInfo.ServerName,
            Message.ServerInfo.Address,
            Message.ServerInfo.Port);
        return;
    }

    if (Message.RequestedType == EServerType::Mgo)
    {
        ApplyMgoServerRoute(
            Message.ServerInfo.ServerId,
            Message.ServerInfo.ServerName,
            Message.ServerInfo.Address,
            Message.ServerInfo.Port);
    }
}

void MWorldServer::Rpc_OnRouterRouteResponse(
    uint64 RequestId,
    uint8 RequestedTypeValue,
    uint64 PlayerId,
    bool bFound,
    uint32 ServerId,
    uint8 ServerTypeValue,
    const FString& ServerName,
    const FString& Address,
    uint16 Port,
    uint16 ZoneId)
{
    SRouteResponseMessage Message;
    Message.RequestId = RequestId;
    Message.RequestedType = static_cast<EServerType>(RequestedTypeValue);
    Message.PlayerId = PlayerId;
    Message.bFound = bFound;
    if (bFound)
    {
        Message.ServerInfo = SServerInfo(
            ServerId,
            static_cast<EServerType>(ServerTypeValue),
            ServerName,
            Address,
            Port,
            ZoneId);
    }

    OnRouter_RouteResponse(Message);
}

void MWorldServer::OnLogin_SessionValidateResponseMessage(const SSessionValidateResponseMessage& Message)
{
    OnLogin_SessionValidateResponse(Message.ConnectionId, Message.PlayerId, Message.bValid);
}

void MWorldServer::OnLogin_SessionValidateResponse(uint64 ConnectionId, uint64 PlayerId, bool bValid)
{
    auto PendingIt = PendingSessionValidations.find(ConnectionId);
    if (PendingIt == PendingSessionValidations.end())
    {
        return;
    }

    const SPendingSessionValidation Pending = PendingIt->second;
    PendingSessionValidations.erase(PendingIt);

    if (!bValid || Pending.PlayerId != PlayerId)
    {
        LOG_WARN("Session validation failed for player %llu on connection %llu",
                 (unsigned long long)Pending.PlayerId,
                 (unsigned long long)ConnectionId);
        return;
    }

    FinalizePlayerLogin(
        PlayerId,
        Pending.GatewayConnectionId,
        Pending.SessionKey,
        false,
        0,
        "",
        "");

    if (Config.EnableMgoPersistence && MgoServerConn && MgoServerConn->IsConnected())
    {
        (void)RequestMgoLoad(PlayerId, Pending.GatewayConnectionId, Pending.SessionKey);
    }
}

void MWorldServer::Rpc_OnPlayerLoginRequest(uint64 ClientConnectionId, uint64 PlayerId, uint32 SessionKey)
{
    const uint64 GatewayBackendConnectionId = FindAuthenticatedBackendConnectionId(EServerType::Gateway);
    if (GatewayBackendConnectionId == 0)
    {
        LOG_WARN("No authenticated Gateway backend available for player login request (ClientConnId=%llu, PlayerId=%llu)",
                 (unsigned long long)ClientConnectionId,
                 (unsigned long long)PlayerId);
        return;
    }

    RequestSessionValidation(GatewayBackendConnectionId, PlayerId, SessionKey);
}

void MWorldServer::Rpc_OnSessionValidateResponse(uint64 ValidationRequestId, uint64 PlayerId, bool bValid)
{
    OnLogin_SessionValidateResponse(ValidationRequestId, PlayerId, bValid);
}

void MWorldServer::Rpc_OnMgoLoadSnapshotResponse(
    uint64 RequestId,
    uint64 ObjectId,
    bool bFound,
    uint16 ClassId,
    const FString& ClassName,
    const FString& SnapshotHex)
{
    auto PendingIt = PendingMgoLoads.find(RequestId);
    if (PendingIt == PendingMgoLoads.end())
    {
        return;
    }

    const SPendingMgoLoad Pending = PendingIt->second;
    PendingMgoLoads.erase(PendingIt);

    if (Pending.PlayerId != ObjectId)
    {
        LOG_WARN("Mgo load response mismatch: request=%llu expected_player=%llu got_object=%llu",
                 static_cast<unsigned long long>(RequestId),
                 static_cast<unsigned long long>(Pending.PlayerId),
                 static_cast<unsigned long long>(ObjectId));
        return;
    }

    if (!bFound)
    {
        return;
    }

    SPlayer* Player = GetPlayerById(Pending.PlayerId);
    if (!Player || !Player->Avatar)
    {
        return;
    }

    if (!ApplyLoadedSnapshotToPlayer(*Player, ClassId, ClassName, SnapshotHex))
    {
        LOG_WARN("Apply async loaded snapshot failed for player %llu", static_cast<unsigned long long>(Pending.PlayerId));
        return;
    }

    LOG_DEBUG("Applied async loaded snapshot: request=%llu player=%llu class=%s",
              static_cast<unsigned long long>(RequestId),
              static_cast<unsigned long long>(Pending.PlayerId),
              ClassName.c_str());
    (void)SendInventoryPullToPlayer(Pending.PlayerId);
}

void MWorldServer::Rpc_OnMgoPersistSnapshotResult(
    uint32 OwnerWorldId,
    uint64 RequestId,
    uint64 ObjectId,
    uint64 Version,
    bool bSuccess,
    const FString& Reason)
{
    if (OwnerWorldId != Config.OwnerServerId)
    {
        return;
    }

    auto It = PendingMgoPersists.find(RequestId);
    if (It == PendingMgoPersists.end())
    {
        ++PersistAckUnmatchedCount;
        return;
    }

    PendingMgoPersists.erase(It);
    if (bSuccess)
    {
        ++PersistAckSuccessCount;
        return;
    }

    ++PersistAckFailedCount;
    LOG_WARN("Mgo persist ACK failed: request=%llu object=%llu version=%llu reason=%s",
             static_cast<unsigned long long>(RequestId),
             static_cast<unsigned long long>(ObjectId),
             static_cast<unsigned long long>(Version),
             Reason.c_str());
}

void MWorldServer::OnPersistRequestDispatched(uint64 RequestId, uint64 ObjectId, uint64 Version)
{
    PendingMgoPersists[RequestId] = SPendingMgoPersist{
        RequestId,
        ObjectId,
        Version,
        MTime::GetTimeSeconds(),
    };
}

bool MWorldServer::RequestMgoLoad(uint64 PlayerId, uint64 GatewayConnectionId, uint32 SessionKey)
{
    if (!MgoServerConn || !MgoServerConn->IsConnected())
    {
        return false;
    }

    uint64 RequestId = NextMgoLoadRequestId++;
    if (RequestId == 0)
    {
        RequestId = NextMgoLoadRequestId++;
    }

    PendingMgoLoads[RequestId] = SPendingMgoLoad{
        RequestId,
        GatewayConnectionId,
        PlayerId,
        SessionKey,
    };

    const bool bSent = MRpc::MMgoService::Rpc_OnLoadSnapshotRequest(
        MgoServerConn,
        RequestId,
        PlayerId);
    if (!bSent)
    {
        PendingMgoLoads.erase(RequestId);
        LOG_WARN("World->Mgo load request send failed: request=%llu player=%llu",
                 static_cast<unsigned long long>(RequestId),
                 static_cast<unsigned long long>(PlayerId));
        return false;
    }

    LOG_DEBUG("World requested Mgo load: request=%llu player=%llu",
              static_cast<unsigned long long>(RequestId),
              static_cast<unsigned long long>(PlayerId));
    return true;
}

void MWorldServer::FinalizePlayerLogin(
    uint64 PlayerId,
    uint64 GatewayConnectionId,
    uint32 SessionKey,
    bool bApplyLoadedSnapshot,
    uint16 LoadedClassId,
    const FString& LoadedClassName,
    const FString& LoadedSnapshotHex)
{
    AddPlayer(PlayerId, "Player" + MString::ToString(PlayerId), GatewayConnectionId);
    auto* Player = GetPlayerById(PlayerId);
    if (!Player)
    {
        return;
    }

    Player->SessionKey = SessionKey;

    if (!bApplyLoadedSnapshot || !Player->Avatar)
    {
        (void)SendInventoryPullToPlayer(PlayerId);
        return;
    }

    if (!ApplyLoadedSnapshotToPlayer(*Player, LoadedClassId, LoadedClassName, LoadedSnapshotHex))
    {
        LOG_WARN("Apply loaded snapshot failed for player %llu", static_cast<unsigned long long>(PlayerId));
    }
    (void)SendInventoryPullToPlayer(PlayerId);
}

bool MWorldServer::ApplyLoadedSnapshotToPlayer(SPlayer& Player, uint16 ClassId, const FString& ClassName, const FString& SnapshotHex)
{
    if (!Player.Avatar)
    {
        return false;
    }

    TArray SnapshotBytes;
    if (!TryDecodeHex(SnapshotHex, SnapshotBytes))
    {
        LOG_WARN("Loaded snapshot decode failed: player=%llu invalid_hex", static_cast<unsigned long long>(Player.PlayerId));
        return false;
    }

    MClass* ClassMeta = Player.Avatar->GetClass();
    if (!ClassMeta)
    {
        return false;
    }

    if (!ClassName.empty() && ClassName != ClassMeta->GetName())
    {
        LOG_WARN("Loaded snapshot class mismatch: player=%llu avatar=%s snapshot=%s class_id=%u",
                 static_cast<unsigned long long>(Player.PlayerId),
                 ClassMeta->GetName().c_str(),
                 ClassName.c_str(),
                 static_cast<unsigned>(ClassId));
        return false;
    }

    ClassMeta->ReadSnapshotByDomain(
        Player.Avatar,
        SnapshotBytes,
        ToMask(EPropertyDomainFlags::Persistence));
    return true;
}

void MWorldServer::SendRouterRegister()
{
    if (!RouterServerConn || !RouterServerConn->IsConnected())
    {
        return;
    }

    SendTypedServerMessage(
        RouterServerConn,
        EServerMessageType::MT_ServerRegister,
        SServerRegisterMessage{Config.OwnerServerId, EServerType::World, Config.ServerName, "127.0.0.1", Config.ListenPort, Config.ZoneId});
}

void MWorldServer::QueryLoginServerRoute()
{
    if (!RouterServerConn || !RouterServerConn->IsConnected())
    {
        return;
    }

    SendTypedServerMessage(
        RouterServerConn,
        EServerMessageType::MT_RouteQuery,
        SRouteQueryMessage{NextRouteRequestId++, EServerType::Login, 0, 0});
}

void MWorldServer::QueryMgoServerRoute()
{
    if (!RouterServerConn || !RouterServerConn->IsConnected())
    {
        return;
    }

    SendTypedServerMessage(
        RouterServerConn,
        EServerMessageType::MT_RouteQuery,
        SRouteQueryMessage{NextRouteRequestId++, EServerType::Mgo, 0, 0});
}

void MWorldServer::SendLoadReport()
{
    if (!RouterServerConn || !RouterServerConn->IsConnected())
    {
        return;
    }

    uint32 OnlineCount = 0;
    for (const auto& [PlayerId, Player] : Players)
    {
        (void)PlayerId;
        if (Player.bOnline)
        {
            ++OnlineCount;
        }
    }

    SendTypedServerMessage(
        RouterServerConn,
        EServerMessageType::MT_ServerLoadReport,
        SServerLoadReportMessage{OnlineCount, Config.MaxPlayers});
}

void MWorldServer::ApplyLoginServerRoute(uint32 ServerId, const FString& ServerName, const FString& Address, uint16 Port)
{
    if (!LoginServerConn)
    {
        return;
    }

    const SServerConnectionConfig& CurrentConfig = LoginServerConn->GetConfig();
    const bool bRouteChanged =
        CurrentConfig.ServerId != ServerId ||
        CurrentConfig.ServerName != ServerName ||
        CurrentConfig.Address != Address ||
        CurrentConfig.Port != Port;

    if (bRouteChanged && (LoginServerConn->IsConnected() || LoginServerConn->IsConnecting()))
    {
        LoginServerConn->Disconnect();
    }

    SServerConnectionConfig NewConfig(ServerId, EServerType::Login, ServerName, Address, Port);
    NewConfig.HeartbeatInterval = CurrentConfig.HeartbeatInterval;
    NewConfig.ConnectTimeout = CurrentConfig.ConnectTimeout;
    NewConfig.ReconnectInterval = CurrentConfig.ReconnectInterval;
    LoginServerConn->SetConfig(NewConfig);

    if (!LoginServerConn->IsConnected() && !LoginServerConn->IsConnecting())
    {
        LoginServerConn->Connect();
    }
}

void MWorldServer::ApplyMgoServerRoute(uint32 ServerId, const FString& ServerName, const FString& Address, uint16 Port)
{
    if (!MgoServerConn)
    {
        return;
    }

    const SServerConnectionConfig& CurrentConfig = MgoServerConn->GetConfig();
    const bool bRouteChanged =
        CurrentConfig.ServerId != ServerId ||
        CurrentConfig.ServerName != ServerName ||
        CurrentConfig.Address != Address ||
        CurrentConfig.Port != Port;

    if (bRouteChanged && (MgoServerConn->IsConnected() || MgoServerConn->IsConnecting()))
    {
        MgoServerConn->Disconnect();
    }

    SServerConnectionConfig NewConfig(ServerId, EServerType::Mgo, ServerName, Address, Port);
    NewConfig.HeartbeatInterval = CurrentConfig.HeartbeatInterval;
    NewConfig.ConnectTimeout = CurrentConfig.ConnectTimeout;
    NewConfig.ReconnectInterval = CurrentConfig.ReconnectInterval;
    MgoServerConn->SetConfig(NewConfig);

    if (!MgoServerConn->IsConnected() && !MgoServerConn->IsConnecting())
    {
        MgoServerConn->Connect();
    }
}
