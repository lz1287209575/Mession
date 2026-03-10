#include "Core/NetCore.h"
#include "Game/GameServer.h"
#include <csignal>
#include <iostream>

// 全局服务器实例
AGameServer* GServer = nullptr;

// 信号处理
void SignalHandler(int32 Signal)
{
    if (GServer)
    {
        LOG_INFO("Received signal %d, shutting down...", Signal);
        GServer->Shutdown();
    }
    exit(0);
}

// 用法
void PrintUsage(const char* ProgramName)
{
    printf("Usage: %s [port]\n", ProgramName);
    printf("  port: Server port (default: 7777)\n");
}

int32 main(int32 argc, char* argv[])
{
    // 解析参数
    uint16 Port = 7777;
    
    if (argc > 1)
    {
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)
        {
            PrintUsage(argv[0]);
            return 0;
        }
        
        Port = (uint16)atoi(argv[1]);
        if (Port == 0)
        {
            printf("Invalid port: %s\n", argv[1]);
            PrintUsage(argv[0]);
            return 1;
        }
    }
    
    // 设置信号处理
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);
    
    // 创建服务器
    GServer = new AGameServer();
    
    // 启动服务器
    if (!GServer->Start(Port))
    {
        LOG_ERROR("Failed to start server");
        delete GServer;
        return 1;
    }
    
    // 运行服务器
    GServer->Run();
    
    // 清理
    delete GServer;
    GServer = nullptr;
    
    return 0;
}
