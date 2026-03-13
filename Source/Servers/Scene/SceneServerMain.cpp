#include "SceneServer.h"
#include "Common/ParseArgs.h"
#include <csignal>

static MSceneServer* GSceneServer = nullptr;

void SignalHandler(int Signal)
{
    (void)Signal;
    LOG_INFO("Received signal, graceful shutdown...");
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

    const double StartTime = MTime::GetTimeSeconds();
    if (!Server.Init(Port > 0 ? Port : 0))
    {
        LOG_ERROR("Failed to start SceneServer");
        return 1;
    }
    MLogger::LogStarted("SceneServer", MTime::GetTimeSeconds() - StartTime);

    Server.Run();
    GSceneServer = nullptr;

    return 0;
}
