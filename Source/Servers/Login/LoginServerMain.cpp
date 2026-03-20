#include "LoginServer.h"
#include "Common/Runtime/Time.h"
#include "Common/Runtime/ParseArgs.h"
#include <csignal>
#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#endif

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
