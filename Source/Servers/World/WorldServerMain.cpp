#include "Servers/World/WorldServer.h"
#include "Servers/App/ServerMain.h"

int main(int argc, char** argv)
{
    return RunMessionServerMain<MWorldServer>(argc, argv);
}
