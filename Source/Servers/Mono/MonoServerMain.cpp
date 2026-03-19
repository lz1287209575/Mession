#include "MonoServer.h"
#include "Common/Logger.h"
#include "Common/MLib.h"

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;
    
    MLogger::LogStartupBanner("MonoServer", 0, 0);
    
    MMonoServer Server;
    
    const double StartTime = MTime::GetTimeSeconds();
    if (!Server.Init())
    {
        LOG_ERROR("Failed to init MonoServer");
        return 1;
    }
    
    MLogger::LogStarted("MonoServer", MTime::GetTimeSeconds() - StartTime);
    
    const bool bOk = Server.Run();
    return bOk ? 0 : 1;
}

