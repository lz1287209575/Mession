#include "GatewayServer.h"
#include "Common/ParseArgs.h"
#include <csignal>

static MGatewayServer* GGatewayServer = nullptr;

void SignalHandler(int Signal)
{
    (void)Signal;
    LOG_INFO("Received signal, graceful shutdown...");
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

    const double StartTime = MTime::GetTimeSeconds();
    if (!Server.Init(Port > 0 ? Port : 0))
    {
        LOG_ERROR("Failed to start GatewayServer");
        return 1;
    }
    MLogger::LogStarted("GatewayServer", MTime::GetTimeSeconds() - StartTime);

    Server.Run();
    GGatewayServer = nullptr;

    return 0;
}
