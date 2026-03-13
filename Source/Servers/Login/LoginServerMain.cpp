#include "LoginServer.h"
#include "Common/ParseArgs.h"
#include <csignal>

static MLoginServer* GLoginServer = nullptr;

void SignalHandler(int Signal)
{
    (void)Signal;
    LOG_INFO("Received signal, graceful shutdown...");
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

    const double StartTime = MTime::GetTimeSeconds();
    if (!Server.Init(Port > 0 ? Port : 0))
    {
        LOG_ERROR("Failed to start LoginServer");
        return 1;
    }
    MLogger::LogStarted("LoginServer", MTime::GetTimeSeconds() - StartTime);

    Server.Run();
    GLoginServer = nullptr;

    return 0;
}
