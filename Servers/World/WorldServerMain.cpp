#include "WorldServer.h"
#include <csignal>
#include <iostream>

void SignalHandler(int Signal)
{
    printf("Received signal %d, shutting down...\n", Signal);
    exit(0);
}

int main(int /*argc*/, char* /*argv*/[])
{
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);
    
    MWorldServer Server;
    
    if (!Server.Init(8003))
    {
        printf("Failed to start WorldServer\n");
        return 1;
    }
    
    Server.Run();
    
    return 0;
}
