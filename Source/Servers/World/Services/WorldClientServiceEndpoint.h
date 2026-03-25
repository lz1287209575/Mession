#pragma once

#include "Common/Runtime/Reflect/Reflection.h"
#include "Protocol/Messages/Gateway/GatewayClientMessages.h"
#include "Servers/App/ClientCallAsyncSupport.h"
#include "Servers/World/Rpc/WorldBackendRpc.h"
#include "Servers/World/Services/WorldPlayerServiceEndpoint.h"

namespace MWorldClientFlows
{
class FClientLoginWorkflow;
}

MCLASS(Type=Service)
class MWorldClientServiceEndpoint : public MObject
{
public:
    MGENERATED_BODY(MWorldClientServiceEndpoint, MObject, 0)
public:
    void Initialize(
        MWorldPlayerServiceEndpoint* InPlayerService,
        MWorldLoginRpc* InLoginRpc);

    MFUNCTION(ClientCall, Target=World)
    void Client_Login(FClientLoginRequest& Request, FClientLoginResponse& Response);

    MFUNCTION(ClientCall, Target=World)
    void Client_FindPlayer(FClientFindPlayerRequest& Request, FClientFindPlayerResponse& Response);

    MFUNCTION(ClientCall, Target=World)
    void Client_Logout(FClientLogoutRequest& Request, FClientLogoutResponse& Response);

    MFUNCTION(ClientCall, Target=World)
    void Client_SwitchScene(FClientSwitchSceneRequest& Request, FClientSwitchSceneResponse& Response);

private:
    friend class MWorldClientFlows::FClientLoginWorkflow;

    MFuture<TResult<FClientLoginResponse, FAppError>> StartClientLoginFlow(
        const FClientLoginRequest& Request,
        uint64 GatewayConnectionId);
    MFuture<TResult<FClientFindPlayerResponse, FAppError>> StartClientFindPlayerFlow(
        const FClientFindPlayerRequest& Request);
    MFuture<TResult<FClientLogoutResponse, FAppError>> StartClientLogoutFlow(
        const FClientLogoutRequest& Request);
    MFuture<TResult<FClientSwitchSceneResponse, FAppError>> StartClientSwitchSceneFlow(
        const FClientSwitchSceneRequest& Request);

    MWorldPlayerServiceEndpoint* PlayerService = nullptr;
    MWorldLoginRpc* LoginRpc = nullptr;
};
