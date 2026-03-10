#include "SceneServer.h"
#include <poll.h>

FSceneServer::FSceneServer()
{
}

bool FSceneServer::Init(int InPort)
{
    // 创建监听socket
    ListenSocket = FSocket::CreateListenSocket((uint16)InPort);
    if (ListenSocket < 0)
    {
        printf("ERROR: Failed to create listen socket on port %d\n", InPort);
        return false;
    }
    
    // 创建默认场景
    CreateDefaultScenes();
    
    bRunning = true;
    
    printf("=====================================\n");
    printf("  Mession Scene Server\n");
    printf("  Listening on port %d (fd=%d)\n", InPort, ListenSocket);
    printf("=====================================\n");
    
    return true;
}

void FSceneServer::Shutdown()
{
    if (!bRunning)
        return;
    
    bRunning = false;
    
    // 关闭世界服务器连接
    if (WorldServerConn)
        WorldServerConn->Close();
    
    // 清理场景
    Scenes.clear();
    
    // 关闭监听socket
    if (ListenSocket >= 0)
    {
        FSocket::Close(ListenSocket);
        ListenSocket = -1;
    }
    
    LOG_INFO("Scene server shutdown complete");
}

void FSceneServer::Tick()
{
    if (!bRunning)
        return;
    
    // 处理世界服务器消息
    ProcessWorldServerMessages();
    
    // 更新场景逻辑（每个场景的AI等）
    // TODO: 实现场景更新
}

void FSceneServer::Run()
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

void FSceneServer::ConnectToWorldServer()
{
    // 连接到世界服务器
    // TODO: 实现
    
    LOG_INFO("Connecting to world server...");
}

void FSceneServer::ProcessWorldServerMessages()
{
    if (!WorldServerConn || !WorldServerConn->IsConnected())
        return;
    
    uint8 Buffer[8192];
    uint32 BytesRead = 0;
    
    while (WorldServerConn->Receive(Buffer, sizeof(Buffer), BytesRead))
    {
        if (BytesRead > 0)
        {
            TArray Data(Buffer, Buffer + BytesRead);
            HandleWorldPacket(Data);
        }
    }
}

void FSceneServer::HandleWorldPacket(const TArray& Data)
{
    if (Data.empty())
        return;
    
    uint8 MsgType = Data[0];
    
    switch (MsgType)
    {
        case 1: // 进入场景
        {
            if (Data.size() < 17)
                return;
            
            uint64 PlayerId = *(uint64*)&Data[1];
            uint16 SceneId = *(uint16*)&Data[9];
            FVector Position;
            Position.X = *(float*)&Data[11];
            Position.Y = *(float*)&Data[15];
            Position.Z = *(float*)&Data[19];
            
            auto Scene = GetScene(SceneId);
            if (Scene)
            {
                FSceneEntity Entity;
                Entity.EntityId = PlayerId;
                Entity.Position = Position;
                Scene->AddEntity(Entity);
                
                LOG_INFO("Player %llu entered scene %d at (%.2f, %.2f, %.2f)",
                        (unsigned long long)PlayerId, SceneId, Position.X, Position.Y, Position.Z);
            }
            break;
        }
        
        case 2: // 离开场景
        {
            if (Data.size() < 11)
                return;
            
            uint64 PlayerId = *(uint64*)&Data[1];
            uint16 SceneId = *(uint16*)&Data[9];
            
            auto Scene = GetScene(SceneId);
            if (Scene)
            {
                Scene->RemoveEntity(PlayerId);
                
                LOG_INFO("Player %llu left scene %d",
                        (unsigned long long)PlayerId, SceneId);
            }
            break;
        }
        
        case 3: // 位置更新
        {
            if (Data.size() < 21)
                return;
            
            uint64 PlayerId = *(uint64*)&Data[1];
            uint16 SceneId = *(uint16*)&Data[9];
            FVector Position;
            Position.X = *(float*)&Data[11];
            Position.Y = *(float*)&Data[15];
            Position.Z = *(float*)&Data[19];
            
            auto Scene = GetScene(SceneId);
            if (Scene)
            {
                Scene->UpdateEntityPosition(PlayerId, Position);
            }
            break;
        }
    }
}

void FSceneServer::CreateDefaultScenes()
{
    // 创建默认场景
    auto Scene = std::make_shared<FScene>(Config.SceneId, Config.SceneName, Config.SceneSize);
    Scenes[Config.SceneId] = Scene;
    
    LOG_INFO("Created scene: %s (id=%d)", Config.SceneName.c_str(), Config.SceneId);
}

std::shared_ptr<FScene> FSceneServer::GetScene(uint16 SceneId)
{
    auto It = Scenes.find(SceneId);
    return (It != Scenes.end()) ? It->second : nullptr;
}

// FScene implementation
void FScene::AddEntity(const FSceneEntity& Entity)
{
    Entities[Entity.EntityId] = Entity;
}

void FScene::RemoveEntity(uint64 EntityId)
{
    Entities.erase(EntityId);
}

void FScene::UpdateEntityPosition(uint64 EntityId, const FVector& NewPosition)
{
    auto It = Entities.find(EntityId);
    if (It != Entities.end())
    {
        It->second.Position = NewPosition;
    }
}
