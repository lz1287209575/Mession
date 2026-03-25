#include "Servers/Scene/SceneServer.h"
#include "Servers/App/ServerMain.h"

int main(int argc, char** argv)
{
    return RunMessionServerMain<MSceneServer>(argc, argv);
}
