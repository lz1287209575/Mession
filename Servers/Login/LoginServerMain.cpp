#include "LoginServer.h"
#include "Common/ParseArgs.h"
#include <csignal>
#include <iostream>

static MLoginServer* GLoginServer = nullptr;

void SignalHandler(int Signal)
{
    (void)Signal;
    printf("Received signal, graceful shutdown...\n");
    if (GLoginServer)
    {
        GLoginServer->RequestShutdown();
    }
}

int main(int argc, char* argv[])
{
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    FString ConfigPath;
    int Port = 8002;
    MParseArgs::Parse(argc, argv, ConfigPath, Port, 8002);

    MLoginServer Server;
    GLoginServer = &Server;
    Server.LoadConfig(ConfigPath);
    if (!Server.Init(Port > 0 ? Port : 0))
    {
        printf("Failed to start LoginServer\n");
        return 1;
    }

    Server.Run();
    GLoginServer = nullptr;

    return 0;
}
