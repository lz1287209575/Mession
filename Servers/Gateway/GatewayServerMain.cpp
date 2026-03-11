#include "GatewayServer.h"
#include "Common/ParseArgs.h"
#include <csignal>
#include <iostream>

static MGatewayServer* GGatewayServer = nullptr;

void SignalHandler(int Signal)
{
    (void)Signal;
    printf("Received signal, graceful shutdown...\n");
    if (GGatewayServer)
    {
        GGatewayServer->RequestShutdown();
    }
}

int main(int argc, char* argv[])
{
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    FString ConfigPath;
    int Port = 8001;
    MParseArgs::Parse(argc, argv, ConfigPath, Port, 8001);

    MGatewayServer Server;
    GGatewayServer = &Server;
    Server.LoadConfig(ConfigPath);
    if (!Server.Init(Port > 0 ? Port : 0))
    {
        printf("Failed to start GatewayServer\n");
        return 1;
    }

    Server.Run();
    GGatewayServer = nullptr;

    return 0;
}
