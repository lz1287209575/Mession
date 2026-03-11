#include "GatewayServer.h"
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
    
    MGatewayServer Server;
    
    if (!Server.Init(8001))
    {
        printf("Failed to start GatewayServer\n");
        return 1;
    }
    
    Server.Run();
    
    return 0;
}
