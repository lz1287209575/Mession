#include "Servers/Router/RouterServer.h"
#include "Servers/App/ServerMain.h"

int main(int argc, char** argv)
{
    return RunMessionServerMain<MRouterServer>(argc, argv);
}
