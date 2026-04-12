#include "Servers/World/WorldClient.h"
#include "Servers/World/WorldClientCommon.h"

#define M_WORLD_CLIENT_PLAYER_ROUTE(MethodName, PlayerMethodName, ClientRequestType, ClientResponseType, FailureCode) \
void MWorldClient::Client_##MethodName(ClientRequestType& Request, ClientResponseType& Response) \
{ \
    PlayerRequest().MethodName(Request, Response); \
}
#include "Servers/World/WorldClientPlayerList.inl"
#undef M_WORLD_CLIENT_PLAYER_ROUTE
