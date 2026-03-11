#include "WorldServer.h"
#include "Common/ParseArgs.h"
#include <csignal>
#include <iostream>

static MWorldServer* GWorldServer = nullptr;

void SignalHandler(int Signal)
{
    (void)Signal;
    printf("Received signal, graceful shutdown...\n");
    if (GWorldServer)
    {
        GWorldServer->RequestShutdown();
    }
}

int main(int argc, char* argv[])
{
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    FString ConfigPath;
    int Port = 8003;
    MParseArgs::Parse(argc, argv, ConfigPath, Port, 8003);

    MWorldServer Server;
    GWorldServer = &Server;
    Server.LoadConfig(ConfigPath);
    if (!Server.Init(Port > 0 ? Port : 0))
    {
        printf("Failed to start WorldServer\n");
        return 1;
    }

    Server.Run();
    GWorldServer = nullptr;

    return 0;
}
