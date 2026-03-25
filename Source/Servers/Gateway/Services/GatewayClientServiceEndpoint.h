#pragma once

#include "Common/Runtime/Reflect/Reflection.h"
#include "Protocol/Messages/Common/AppMessages.h"
#include "Protocol/Messages/Auth/AuthSessionMessages.h"
#include "Protocol/Messages/Gateway/GatewayClientMessages.h"
#include "Protocol/Messages/World/WorldPlayerMessages.h"
#include "Servers/App/ClientCallAsyncSupport.h"
#include "Servers/Gateway/Rpc/GatewayBackendRpc.h"

MCLASS(Type=Service)
class MGatewayClientServiceEndpoint : public MObject
{
public:
    MGENERATED_BODY(MGatewayClientServiceEndpoint, MObject, 0)
public:
    void Initialize(MGatewayLoginRpc* InLoginRpc, MGatewayWorldRpc* InWorldRpc);

    MFUNCTION(ClientCall)
    void Client_Echo(FClientEchoRequest& Request, FClientEchoResponse& Response);

    MFUNCTION(ClientCall)
    void Client_Login(FClientLoginRequest& Request, FClientLoginResponse& Response);

    MFUNCTION(ClientCall)
    void Client_FindPlayer(FClientFindPlayerRequest& Request, FClientFindPlayerResponse& Response);

    MFUNCTION(ClientCall)
    void Client_Logout(FClientLogoutRequest& Request, FClientLogoutResponse& Response);

    MFUNCTION(ClientCall)
    void Client_SwitchScene(FClientSwitchSceneRequest& Request, FClientSwitchSceneResponse& Response);

private:
    MFuture<TResult<FClientLoginResponse, FAppError>> StartLoginFlow(
        const FClientLoginRequest& Request,
        uint64 GatewayConnectionId) const;
    MFuture<TResult<FClientFindPlayerResponse, FAppError>> StartFindPlayerFlow(
        const FClientFindPlayerRequest& Request) const;
    MFuture<TResult<FClientLogoutResponse, FAppError>> StartLogoutFlow(
        const FClientLogoutRequest& Request) const;
    MFuture<TResult<FClientSwitchSceneResponse, FAppError>> StartSwitchSceneFlow(
        const FClientSwitchSceneRequest& Request) const;

    MGatewayLoginRpc* LoginRpc = nullptr;
    MGatewayWorldRpc* WorldRpc = nullptr;
};
