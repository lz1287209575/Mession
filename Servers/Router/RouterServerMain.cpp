#include "RouterServer.h"
#include "Common/ParseArgs.h"
#include <csignal>
#include <iostream>

static MRouterServer* GRouterServer = nullptr;

void SignalHandler(int Signal)
{
    (void)Signal;
    printf("Received signal, graceful shutdown...\n");
    if (GRouterServer)
    {
        GRouterServer->RequestShutdown();
    }
}

int main(int argc, char* argv[])
{
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    FString ConfigPath;
    int Port = 8005;
    MParseArgs::Parse(argc, argv, ConfigPath, Port, 8005);

    MRouterServer Server;
    GRouterServer = &Server;
    Server.LoadConfig(ConfigPath);
    if (!Server.Init(Port > 0 ? Port : 0))
    {
        printf("Failed to start RouterServer\n");
        return 1;
    }

    Server.Run();
    GRouterServer = nullptr;

    return 0;
}
