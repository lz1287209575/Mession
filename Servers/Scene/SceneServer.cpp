#include "SceneServer.h"
#include "Common/Config.h"
#include "Common/ServerMessages.h"
#include "Core/Poll.h"

namespace
{
const TMap<FString, const char*> SceneEnvMap = {
    {"port", "MESSION_SCENE_PORT"},
    {"router_addr", "MESSION_ROUTER_ADDR"},
    {"router_port", "MESSION_ROUTER_PORT"},
    {"zone_id", "MESSION_ZONE_ID"},
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
    return true;
}

bool MSceneServer::Init(int InPort)
{
    if (InPort > 0)
    {
        Config.ListenPort = static_cast<uint16>(InPort);
    }
    // 创建监听socket
    ListenSocket = MSocket::CreateListenSocket(Config.ListenPort);
    if (ListenSocket == INVALID_SOCKET_FD)
    {
        printf("ERROR: Failed to create listen socket on port %d\n", Config.ListenPort);
        return false;
    }

    // 创建默认场景
    CreateDefaultScenes();

    ConnectToRouterServer();

    bRunning = true;

    printf("=====================================\n");
    printf("  Mession Scene Server\n");
    printf("  Listening on port %d (fd=%zd)\n", Config.ListenPort, (intptr_t)ListenSocket);
    printf("=====================================\n");
    
    return true;
}

void MSceneServer::RequestShutdown()
{
    bRunning = false;
    if (ListenSocket != INVALID_SOCKET_FD)
    {
        MSocket::Close(ListenSocket);
        ListenSocket = INVALID_SOCKET_FD;
    }
}

void MSceneServer::Shutdown()
{
    if (bShutdownDone)
    {
        return;
    }
    bShutdownDone = true;
    bRunning = false;
    
    // 关闭世界服务器连接
    if (RouterServerConn)
    {
        RouterServerConn->Disconnect();
    }
    if (WorldServerConn)
    {
        WorldServerConn->Disconnect();
    }
    
    // 清理场景
    Scenes.clear();
    
    // 关闭监听socket
    if (ListenSocket != INVALID_SOCKET_FD)
    {
        MSocket::Close(ListenSocket);
        ListenSocket = INVALID_SOCKET_FD;
    }
    
    LOG_INFO("Scene server shutdown complete");
}

void MSceneServer::Tick()
{
    if (!bRunning)
    {
        return;
    }
    
    // 处理世界服务器消息
    if (RouterServerConn)
    {
        RouterServerConn->Tick(0.016f);
    }

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

    ProcessWorldServerMessages();

    for (auto& [SceneId, Scene] : Scenes)
    {
        (void)SceneId;
        if (Scene)
        {
            Scene->Tick(0.016f);
        }
    }
}

void MSceneServer::Run()
{
    if (!bRunning)
    {
        LOG_ERROR("Scene server not initialized!");
        return;
    }
    
    LOG_INFO("Scene server running...");
    
    while (bRunning)
    {
        Tick();
        MTime::SleepMilliseconds(16);
    }
}

void MSceneServer::ConnectToRouterServer()
{
    MServerConnection::SetLocalInfo(Config.SceneId, EServerType::Scene, Config.SceneName);

    SServerConnectionConfig RouterConfig(100, EServerType::Router, "Router01", Config.RouterServerAddr, Config.RouterServerPort);
    RouterServerConn = TSharedPtr<MServerConnection>(new MServerConnection(RouterConfig));
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
        WorldServerConn = TSharedPtr<MServerConnection>(new MServerConnection(WorldConfig));
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
            if (!ParsePayload(Data, Message))
            {
                LOG_WARN("Invalid scene route response payload size: %zu", Data.size());
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
        WorldServerConn = TSharedPtr<MServerConnection>(new MServerConnection(WorldConfig));
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

void MSceneServer::ProcessWorldServerMessages()
{
    if (!WorldServerConn)
    {
        return;
    }

    WorldServerConn->Tick(0.016f);
}

void MSceneServer::HandleWorldPacket(uint8 Type, const TArray& Data)
{
    switch ((EServerMessageType)Type)
    {
        case EServerMessageType::MT_PlayerSwitchServer:
        {
            SPlayerSceneStateMessage Message;
            if (!ParsePayload(Data, Message))
            {
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
            if (!ParsePayload(Data, Message))
            {
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
            if (!ParsePayload(Data, Message))
            {
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
    auto Scene = TSharedPtr<MScene>(new MScene(Config.SceneId, Config.SceneName, Config.SceneSize));
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
