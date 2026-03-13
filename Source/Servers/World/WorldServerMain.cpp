#include "WorldServer.h"
#include "Common/ParseArgs.h"
#include <csignal>

static MWorldServer* GWorldServer = nullptr;

void SignalHandler(int Signal)
{
    (void)Signal;
    LOG_INFO("Received signal, graceful shutdown...");
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

    const double StartTime = MTime::GetTimeSeconds();
    if (!Server.Init(Port > 0 ? Port : 0))
    {
        LOG_ERROR("Failed to start WorldServer");
        return 1;
    }
    MLogger::LogStarted("WorldServer", MTime::GetTimeSeconds() - StartTime);

    Server.Run();
    GWorldServer = nullptr;

    return 0;
}
