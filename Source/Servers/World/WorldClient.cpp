#include "Servers/World/WorldClient.h"
#include "Servers/World/WorldClientCommon.h"
#include "Servers/World/WorldServer.h"

void MWorldClient::Initialize(
    MWorldServer* InWorldServer,
    MWorldLogin* InLogin)
{
    WorldServer = InWorldServer;
    Login = InLogin;
}

MWorldClientPlayer::FRequest MWorldClient::PlayerRequest() const
{
    return MWorldClientPlayer::FRequest(WorldServer ? WorldServer->GetPlayerService() : nullptr);
}
