#include "RouterServer.h"
#include "Common/ParseArgs.h"
#include <csignal>

static MRouterServer* GRouterServer = nullptr;

void SignalHandler(int Signal)
{
    (void)Signal;
    LOG_INFO("Received signal, graceful shutdown...");
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

    const double StartTime = MTime::GetTimeSeconds();
    if (!Server.Init(Port > 0 ? Port : 0))
    {
        LOG_ERROR("Failed to start RouterServer");
        return 1;
    }
    MLogger::LogStarted("RouterServer", MTime::GetTimeSeconds() - StartTime);

    Server.Run();
    GRouterServer = nullptr;

    return 0;
}
