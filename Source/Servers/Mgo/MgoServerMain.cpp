#include "Servers/Mgo/MgoServer.h"
#include "Servers/App/ServerMain.h"

int main(int argc, char** argv)
{
    return RunMessionServerMain<MMgoServer>(argc, argv);
}
