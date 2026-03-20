#include "SceneServer.h"
#include "Common/Runtime/Time.h"
#include "Common/Runtime/ParseArgs.h"
#include <csignal>
#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#endif

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

#if defined(_WIN32) || defined(_WIN64)
BOOL WINAPI ConsoleControlHandler(DWORD ControlType)
{
    switch (ControlType)
    {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            SignalHandler(SIGTERM);
            return TRUE;
        default:
            return FALSE;
    }
}
#endif

int main(int argc, char* argv[])
{
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);
#if defined(_WIN32) || defined(_WIN64)
    SetConsoleCtrlHandler(ConsoleControlHandler, TRUE);
#endif

    MString ConfigPath;
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
