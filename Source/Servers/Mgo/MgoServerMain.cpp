#include "MgoServer.h"
#include "Common/Runtime/Time.h"
#include "Common/Runtime/ParseArgs.h"
#include <csignal>

static MMgoServer* GMgoServer = nullptr;

void SignalHandler(int Signal)
{
    (void)Signal;
    LOG_INFO("Received signal, graceful shutdown...");
    if (GMgoServer)
    {
        GMgoServer->RequestShutdown();
    }
}

int main(int argc, char* argv[])
{
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    MString ConfigPath;
    int Port = 8006;
    MParseArgs::Parse(argc, argv, ConfigPath, Port, 8006);

    MMgoServer Server;
    GMgoServer = &Server;
    Server.LoadConfig(ConfigPath);

    const double StartTime = MTime::GetTimeSeconds();
    if (!Server.Init(Port > 0 ? Port : 0))
    {
        LOG_ERROR("Failed to start MgoServer");
        return 1;
    }
    MLogger::LogStarted("MgoServer", MTime::GetTimeSeconds() - StartTime);

    Server.Run();
    GMgoServer = nullptr;
    return 0;
}

