#include "Servers/Gateway/GatewayServer.h"
#include "Servers/App/ServerMain.h"

int main(int argc, char** argv)
{
    return RunMessionServerMain<MGatewayServer>(argc, argv);
}
