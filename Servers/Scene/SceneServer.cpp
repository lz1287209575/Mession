#include "SceneServer.h"
#include <poll.h>

namespace
{
template<typename T>
void AppendValue(TArray& OutData, const T& Value)
{
    const auto* ValueBytes = reinterpret_cast<const uint8*>(&Value);
    OutData.insert(OutData.end(), ValueBytes, ValueBytes + sizeof(T));
}

void AppendString(TArray& OutData, const FString& Value)
{
    const uint16 Length = static_cast<uint16>(Value.size());
    AppendValue(OutData, Length);
    OutData.insert(OutData.end(), Value.begin(), Value.end());
}

template<typename T>
bool ReadValue(const TArray& Data, size_t& Offset, T& OutValue)
{
    if (Offset + sizeof(T) > Data.size())
        return false;

    memcpy(&OutValue, Data.data() + Offset, sizeof(T));
    Offset += sizeof(T);
    return true;
}

bool ReadString(const TArray& Data, size_t& Offset, FString& OutValue)
{
    uint16 Length = 0;
    if (!ReadValue(Data, Offset, Length) || Offset + Length > Data.size())
        return false;

    OutValue.assign(reinterpret_cast<const char*>(Data.data() + Offset), Length);
    Offset += Length;
    return true;
}
}

MSceneServer::MSceneServer()
{
}

bool MSceneServer::Init(int InPort)
{
    Config.ListenPort = static_cast<uint16>(InPort);
    // 创建监听socket
    ListenSocket = MSocket::CreateListenSocket((uint16)InPort);
    if (ListenSocket < 0)
    {
        printf("ERROR: Failed to create listen socket on port %d\n", InPort);
        return false;
    }
    
    // 创建默认场景
    CreateDefaultScenes();

    ConnectToRouterServer();
    
    bRunning = true;
    
    printf("=====================================\n");
    printf("  Mession Scene Server\n");
    printf("  Listening on port %d (fd=%d)\n", InPort, ListenSocket);
    printf("=====================================\n");
    
    return true;
}

void MSceneServer::Shutdown()
{
    if (!bRunning)
        return;
    
    bRunning = false;
    
    // 关闭世界服务器连接
    if (RouterServerConn)
        RouterServerConn->Disconnect();
    if (WorldServerConn)
        WorldServerConn->Disconnect();
    
    // 清理场景
    Scenes.clear();
    
    // 关闭监听socket
    if (ListenSocket >= 0)
    {
        MSocket::Close(ListenSocket);
        ListenSocket = -1;
    }
    
    LOG_INFO("Scene server shutdown complete");
}

void MSceneServer::Tick()
{
    if (!bRunning)
        return;
    
    // 处理世界服务器消息
    if (RouterServerConn)
        RouterServerConn->Tick(0.016f);

    WorldRouteQueryTimer += 0.016f;
    if (RouterServerConn && RouterServerConn->IsConnected() && WorldRouteQueryTimer >= 1.0f)
    {
        WorldRouteQueryTimer = 0.0f;
        if (!WorldServerConn || !WorldServerConn->IsConnected())
            QueryWorldServerRoute();
    }

    ProcessWorldServerMessages();
    
    // 更新场景逻辑（每个场景的AI等）
    // TODO: 实现场景更新
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
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
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
        WorldServerConn->Connect();

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
            size_t Offset = 0;
            uint64 RequestId = 0;
            uint8 RequestedTypeValue = 0;
            uint64 PlayerId = 0;
            uint8 Result = 0;
            if (!ReadValue(Data, Offset, RequestId) ||
                !ReadValue(Data, Offset, RequestedTypeValue) ||
                !ReadValue(Data, Offset, PlayerId) ||
                !ReadValue(Data, Offset, Result))
            {
                LOG_WARN("Invalid scene route response payload size: %zu", Data.size());
                return;
            }

            if (PlayerId != 0 || (EServerType)RequestedTypeValue != EServerType::World || !Result)
                return;

            uint32 ServerId = 0;
            uint8 ServerTypeValue = 0;
            FString ServerName;
            FString Address;
            uint16 Port = 0;
            if (!ReadValue(Data, Offset, ServerId) ||
                !ReadValue(Data, Offset, ServerTypeValue) ||
                !ReadString(Data, Offset, ServerName) ||
                !ReadString(Data, Offset, Address) ||
                !ReadValue(Data, Offset, Port))
            {
                LOG_WARN("Invalid scene world route payload size: %zu", Data.size());
                return;
            }

            ApplyWorldServerRoute(ServerId, ServerName, Address, Port);
            break;
        }

        default:
            break;
    }
}

void MSceneServer::SendRouterRegister()
{
    if (!RouterServerConn || !RouterServerConn->IsConnected())
        return;

    TArray Payload;
    AppendValue(Payload, static_cast<uint32>(Config.SceneId));
    Payload.push_back((uint8)EServerType::Scene);
    AppendString(Payload, Config.SceneName);
    AppendString(Payload, "127.0.0.1");
    AppendValue(Payload, Config.ListenPort);
    RouterServerConn->Send((uint8)EServerMessageType::MT_ServerRegister, Payload.data(), Payload.size());
}

void MSceneServer::QueryWorldServerRoute()
{
    if (!RouterServerConn || !RouterServerConn->IsConnected())
        return;

    TArray Payload;
    AppendValue(Payload, NextRouteRequestId++);
    Payload.push_back((uint8)EServerType::World);
    AppendValue(Payload, static_cast<uint64>(0));
    RouterServerConn->Send((uint8)EServerMessageType::MT_RouteQuery, Payload.data(), Payload.size());
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
        WorldServerConn->Disconnect();

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
        return;

    WorldServerConn->Tick(0.016f);
}

void MSceneServer::HandleWorldPacket(uint8 Type, const TArray& Data)
{
    switch ((EServerMessageType)Type)
    {
        case EServerMessageType::MT_PlayerSwitchServer:
        {
            size_t Offset = 0;
            uint64 PlayerId = 0;
            uint16 SceneId = 0;
            SVector Position;
            if (!ReadValue(Data, Offset, PlayerId) ||
                !ReadValue(Data, Offset, SceneId) ||
                !ReadValue(Data, Offset, Position.X) ||
                !ReadValue(Data, Offset, Position.Y) ||
                !ReadValue(Data, Offset, Position.Z))
                return;

            auto Scene = GetScene(SceneId);
            if (Scene)
            {
                SSceneEntity Entity;
                Entity.EntityId = PlayerId;
                Entity.Position = Position;
                Scene->AddEntity(Entity);
                
                LOG_INFO("Player %llu entered scene %d at (%.2f, %.2f, %.2f)",
                        (unsigned long long)PlayerId, SceneId, Position.X, Position.Y, Position.Z);
            }
            break;
        }

        case EServerMessageType::MT_PlayerLogout:
        {
            size_t Offset = 0;
            uint64 PlayerId = 0;
            uint16 SceneId = 0;
            if (!ReadValue(Data, Offset, PlayerId) ||
                !ReadValue(Data, Offset, SceneId))
                return;

            auto Scene = GetScene(SceneId);
            if (Scene)
            {
                Scene->RemoveEntity(PlayerId);
                
                LOG_INFO("Player %llu left scene %d",
                        (unsigned long long)PlayerId, SceneId);
            }
            break;
        }

        case EServerMessageType::MT_PlayerDataSync:
        {
            size_t Offset = 0;
            uint64 PlayerId = 0;
            uint16 SceneId = 0;
            SVector Position;
            if (!ReadValue(Data, Offset, PlayerId) ||
                !ReadValue(Data, Offset, SceneId) ||
                !ReadValue(Data, Offset, Position.X) ||
                !ReadValue(Data, Offset, Position.Y) ||
                !ReadValue(Data, Offset, Position.Z))
                return;

            auto Scene = GetScene(SceneId);
            if (Scene)
            {
                Scene->UpdateEntityPosition(PlayerId, Position);
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
