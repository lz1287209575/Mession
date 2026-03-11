#include "SceneServer.h"
#include "Common/ParseArgs.h"
#include <csignal>
#include <iostream>

static MSceneServer* GSceneServer = nullptr;

void SignalHandler(int Signal)
{
    (void)Signal;
    printf("Received signal, graceful shutdown...\n");
    if (GSceneServer)
    {
        GSceneServer->RequestShutdown();
    }
}

int main(int argc, char* argv[])
{
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    FString ConfigPath;
    int Port = 8004;
    MParseArgs::Parse(argc, argv, ConfigPath, Port, 8004);

    MSceneServer Server;
    GSceneServer = &Server;
    Server.LoadConfig(ConfigPath);
    if (!Server.Init(Port > 0 ? Port : 0))
    {
        printf("Failed to start SceneServer\n");
        return 1;
    }

    Server.Run();
    GSceneServer = nullptr;

    return 0;
}
