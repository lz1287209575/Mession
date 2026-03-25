#include "Servers/Login/LoginServer.h"
#include "Servers/App/ServerMain.h"

int main(int argc, char** argv)
{
    return RunMessionServerMain<MLoginServer>(argc, argv);
}
