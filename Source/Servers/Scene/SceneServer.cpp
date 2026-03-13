#include "SceneServer.h"
#include "Common/Config.h"
#include "Common/ServerMessages.h"
#include "Core/Json.h"

namespace
{
const TMap<FString, const char*> SceneEnvMap = {
    {"port", "MESSION_SCENE_PORT"},
    {"router_addr", "MESSION_ROUTER_ADDR"},
    {"router_port", "MESSION_ROUTER_PORT"},
    {"zone_id", "MESSION_ZONE_ID"},
    {"debug_http_port", "MESSION_SCENE_DEBUG_HTTP_PORT"},
};
}

MSceneServer::MSceneServer()
{
}

bool MSceneServer::LoadConfig(const FString& ConfigPath)
{
    TMap<FString, FString> Vars;
    if (!ConfigPath.empty())
    {
        MConfig::LoadFromFile(ConfigPath, Vars);
    }
    MConfig::ApplyEnvOverrides(Vars, SceneEnvMap);
    Config.ListenPort = MConfig::GetU16(Vars, "port", Config.ListenPort);
    Config.RouterServerAddr = MConfig::GetStr(Vars, "router_addr", Config.RouterServerAddr);
    Config.RouterServerPort = MConfig::GetU16(Vars, "router_port", Config.RouterServerPort);
    Config.ZoneId = MConfig::GetU16(Vars, "zone_id", Config.ZoneId);
    Config.DebugHttpPort = MConfig::GetU16(Vars, "debug_http_port", Config.DebugHttpPort);
    return true;
}

bool MSceneServer::Init(int InPort)
{
    if (InPort > 0)
    {
        Config.ListenPort = static_cast<uint16>(InPort);
    }
    // 创建默认场景
    CreateDefaultScenes();

    ConnectToRouterServer();

    bRunning = true;

    MLogger::LogStartupBanner("SceneServer", Config.ListenPort, 0);

    // 启动调试 HTTP 服务器（仅当配置端口 > 0 时）
    if (Config.DebugHttpPort > 0)
    {
        DebugServer = TUniquePtr<MHttpDebugServer>(new MHttpDebugServer(
            Config.DebugHttpPort,
            [this]() { return BuildDebugStatusJson(); }));
        DebugServer->Start();
    }

    return true;
}

uint16 MSceneServer::GetListenPort() const
{
    return Config.ListenPort;
}

void MSceneServer::OnAccept(uint64, TSharedPtr<INetConnection> Conn)
{
    if (Conn)
    {
        Conn->Close();
    }
}

void MSceneServer::ShutdownConnections()
{
    BackendConnectionManager.DisconnectAll();
    RouterServerConn.reset();
    WorldServerConn.reset();
    Scenes.clear();
    if (DebugServer)
    {
        DebugServer->Stop();
        DebugServer.reset();
    }
    LOG_INFO("Scene server shutdown complete");
}

void MSceneServer::OnRunStarted()
{
    LOG_INFO("Scene server running...");
}

void MSceneServer::Tick()
{
    if (!bRunning)
    {
        return;
    }
    TickBackends();
}

void MSceneServer::TickBackends()
{
    // 统一驱动所有后端连接
    BackendConnectionManager.Tick(0.016f);

    WorldRouteQueryTimer += 0.016f;
    if (RouterServerConn && RouterServerConn->IsConnected() && WorldRouteQueryTimer >= 1.0f)
    {
        WorldRouteQueryTimer = 0.0f;
        if (!WorldServerConn || !WorldServerConn->IsConnected())
        {
            QueryWorldServerRoute();
        }
    }

    LoadReportTimer += 0.016f;
    if (RouterServerConn && RouterServerConn->IsConnected() && LoadReportTimer >= 5.0f)
    {
        LoadReportTimer = 0.0f;
        SendLoadReport();
    }

    if (WorldServerConn)
    {
        WorldServerConn->Tick(0.016f);
    }

    for (auto& [SceneId, Scene] : Scenes)
    {
        (void)SceneId;
        if (Scene)
        {
            Scene->Tick(0.016f);
        }
    }
}

FString MSceneServer::BuildDebugStatusJson() const
{
    const SConnectionManagerStats Stats = BackendConnectionManager.GetStats();
    size_t EntityCount = 0;
    for (const auto& [SceneId, Scene] : Scenes)
    {
        (void)SceneId;
        if (Scene)
        {
            EntityCount += Scene->GetEntities().size();
        }
    }

    MJsonWriter W = MJsonWriter::Object();
    W.Key("server"); W.Value("Scene");
    W.Key("scenes"); W.Value(static_cast<uint64>(Scenes.size()));
    W.Key("entities"); W.Value(static_cast<uint64>(EntityCount));
    W.Key("backendTotal"); W.Value(static_cast<uint64>(Stats.Total));
    W.Key("backendActive"); W.Value(static_cast<uint64>(Stats.Active));
    W.Key("bytesSent"); W.Value(static_cast<uint64>(Stats.BytesSent));
    W.Key("bytesReceived"); W.Value(static_cast<uint64>(Stats.BytesReceived));
    W.Key("reconnectAttempts"); W.Value(static_cast<uint64>(Stats.ReconnectAttempts));
    return W.ToString();
}

void MSceneServer::ConnectToRouterServer()
{
    MServerConnection::SetLocalInfo(Config.SceneId, EServerType::Scene, Config.SceneName);

    SServerConnectionConfig RouterConfig(100, EServerType::Router, "Router01", Config.RouterServerAddr, Config.RouterServerPort);
    RouterServerConn = BackendConnectionManager.AddServer(RouterConfig);
    RouterServerConn->SetOnAuthenticated([this](auto, const SServerInfo& Info) {
        LOG_INFO("Connected to router server: %s", Info.ServerName.c_str());
        SendRouterRegister();
        QueryWorldServerRoute();
    });
    RouterServerConn->SetOnMessage([this](auto, uint8 Type, const TArray& Data) {
        HandleRouterServerMessage(Type, Data);
    });
    RouterServerConn->Connect();

    LOG_INFO("Connecting to router server...");
}

void MSceneServer::ConnectToWorldServer()
{
    MServerConnection::SetLocalInfo(Config.SceneId, EServerType::Scene, Config.SceneName);

    if (!WorldServerConn)
    {
        SServerConnectionConfig WorldConfig(3, EServerType::World, "World01", Config.WorldServerAddr, Config.WorldServerPort);
        WorldServerConn = BackendConnectionManager.AddServer(WorldConfig);
    }
    WorldServerConn->SetOnAuthenticated([](auto, const SServerInfo& Info) {
        LOG_INFO("Connected to world server: %s", Info.ServerName.c_str());
    });
    WorldServerConn->SetOnMessage([this](auto, uint8 Type, const TArray& Data) {
        HandleWorldPacket(Type, Data);
    });
    if (!WorldServerConn->IsConnected() && !WorldServerConn->IsConnecting())
    {
        WorldServerConn->Connect();
    }

    LOG_INFO("Connecting to world server...");
}

void MSceneServer::HandleRouterServerMessage(uint8 Type, const TArray& Data)
{
    switch ((EServerMessageType)Type)
    {
        case EServerMessageType::MT_ServerRegisterAck:
            LOG_INFO("Scene server registered to RouterServer");
            break;

        case EServerMessageType::MT_RouteResponse:
        {
            SRouteResponseMessage Message;
            auto ParseResult = ParsePayload(Data, Message, "MT_RouteResponse");
            if (!ParseResult.IsOk())
            {
                LOG_WARN("ParsePayload failed: %s", ParseResult.GetError().c_str());
                return;
            }

            if (Message.PlayerId != 0 || Message.RequestedType != EServerType::World || !Message.bFound)
            {
                return;
            }

            ApplyWorldServerRoute(
                Message.ServerInfo.ServerId,
                Message.ServerInfo.ServerName,
                Message.ServerInfo.Address,
                Message.ServerInfo.Port);
            break;
        }

        default:
            break;
    }
}

void MSceneServer::SendRouterRegister()
{
    if (!RouterServerConn || !RouterServerConn->IsConnected())
    {
        return;
    }

    SendTypedServerMessage(
        RouterServerConn,
        EServerMessageType::MT_ServerRegister,
        SServerRegisterMessage{
            static_cast<uint32>(Config.SceneId),
            EServerType::Scene,
            Config.SceneName,
            "127.0.0.1",
            Config.ListenPort,
            Config.ZoneId
        });
}

void MSceneServer::SendLoadReport()
{
    if (!RouterServerConn || !RouterServerConn->IsConnected())
    {
        return;
    }

    uint32 EntityCount = 0;
    for (const auto& [SceneId, Scene] : Scenes)
    {
        (void)SceneId;
        if (Scene)
        {
            EntityCount += static_cast<uint32>(Scene->GetEntities().size());
        }
    }

    constexpr uint32 MaxSceneEntities = 10000;
    SendTypedServerMessage(
        RouterServerConn,
        EServerMessageType::MT_ServerLoadReport,
        SServerLoadReportMessage{EntityCount, MaxSceneEntities});
}

void MSceneServer::QueryWorldServerRoute()
{
    if (!RouterServerConn || !RouterServerConn->IsConnected())
    {
        return;
    }

    SendTypedServerMessage(
        RouterServerConn,
        EServerMessageType::MT_RouteQuery,
        SRouteQueryMessage{NextRouteRequestId++, EServerType::World, 0, 0});
}

void MSceneServer::ApplyWorldServerRoute(uint32 ServerId, const FString& ServerName, const FString& Address, uint16 Port)
{
    if (!WorldServerConn)
    {
        SServerConnectionConfig WorldConfig(ServerId, EServerType::World, ServerName, Address, Port);
        WorldServerConn = MakeShared<MServerConnection>(WorldConfig);
    }

    const SServerConnectionConfig& CurrentConfig = WorldServerConn->GetConfig();
    const bool bRouteChanged =
        CurrentConfig.ServerId != ServerId ||
        CurrentConfig.ServerName != ServerName ||
        CurrentConfig.Address != Address ||
        CurrentConfig.Port != Port;

    if (bRouteChanged && (WorldServerConn->IsConnected() || WorldServerConn->IsConnecting()))
    {
        WorldServerConn->Disconnect();
    }

    SServerConnectionConfig NewConfig(ServerId, EServerType::World, ServerName, Address, Port);
    NewConfig.HeartbeatInterval = CurrentConfig.HeartbeatInterval;
    NewConfig.ConnectTimeout = CurrentConfig.ConnectTimeout;
    NewConfig.ReconnectInterval = CurrentConfig.ReconnectInterval;
    WorldServerConn->SetConfig(NewConfig);
    ConnectToWorldServer();
}

void MSceneServer::HandleWorldPacket(uint8 Type, const TArray& Data)
{
    switch ((EServerMessageType)Type)
    {
        case EServerMessageType::MT_PlayerSwitchServer:
        {
            SPlayerSceneStateMessage Message;
            auto ParseResult = ParsePayload(Data, Message, "MT_PlayerSwitchServer");
            if (!ParseResult.IsOk())
            {
                LOG_WARN("ParsePayload failed: %s", ParseResult.GetError().c_str());
                return;
            }

            auto Scene = GetScene(Message.SceneId);
            if (Scene)
            {
                SSceneEntity Entity;
                Entity.EntityId = Message.PlayerId;
                Entity.Position = SVector(Message.X, Message.Y, Message.Z);
                Scene->AddEntity(Entity);
                
                LOG_INFO("Player %llu entered scene %d at (%.2f, %.2f, %.2f)",
                        (unsigned long long)Message.PlayerId,
                        Message.SceneId,
                        Message.X,
                        Message.Y,
                        Message.Z);
            }
            break;
        }

        case EServerMessageType::MT_PlayerLogout:
        {
            SPlayerSceneLeaveMessage Message;
            auto ParseResult = ParsePayload(Data, Message, "MT_PlayerLogout");
            if (!ParseResult.IsOk())
            {
                LOG_WARN("ParsePayload failed: %s", ParseResult.GetError().c_str());
                return;
            }

            auto Scene = GetScene(Message.SceneId);
            if (Scene)
            {
                Scene->RemoveEntity(Message.PlayerId);
                
                LOG_INFO("Player %llu left scene %d",
                        (unsigned long long)Message.PlayerId, Message.SceneId);
            }
            break;
        }

        case EServerMessageType::MT_PlayerDataSync:
        {
            SPlayerSceneStateMessage Message;
            auto ParseResult = ParsePayload(Data, Message, "MT_PlayerDataSync");
            if (!ParseResult.IsOk())
            {
                LOG_WARN("ParsePayload failed: %s", ParseResult.GetError().c_str());
                return;
            }

            auto Scene = GetScene(Message.SceneId);
            if (Scene)
            {
                Scene->UpdateEntityPosition(Message.PlayerId, SVector(Message.X, Message.Y, Message.Z));
            }
            break;
        }

        default:
            break;
    }
}

void MSceneServer::CreateDefaultScenes()
{
    // 创建默认场景
    auto Scene = MakeShared<MScene>(Config.SceneId, Config.SceneName, Config.SceneSize);
    Scenes[Config.SceneId] = Scene;
    
    LOG_INFO("Created scene: %s (id=%d)", Config.SceneName.c_str(), Config.SceneId);
}

TSharedPtr<MScene> MSceneServer::GetScene(uint16 SceneId)
{
    auto It = Scenes.find(SceneId);
    return (It != Scenes.end()) ? It->second : nullptr;
}

// MScene implementation
void MScene::AddEntity(const SSceneEntity& Entity)
{
    Entities[Entity.EntityId] = Entity;
}

void MScene::RemoveEntity(uint64 EntityId)
{
    Entities.erase(EntityId);
}

void MScene::UpdateEntityPosition(uint64 EntityId, const SVector& NewPosition)
{
    auto It = Entities.find(EntityId);
    if (It != Entities.end())
    {
        It->second.Position = NewPosition;
    }
}

void MScene::Tick(float DeltaTime)
{
    (void)DeltaTime;
    for (auto& [EntityId, Entity] : Entities)
    {
        (void)EntityId;
        (void)Entity;
        // 预留：NPC AI、定时刷新、状态更新等场景逻辑
    }
}
