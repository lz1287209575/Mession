#pragma once

#include "Common/Runtime/Reflect/Reflection.h"
#include "Protocol/Messages/Auth/AuthSessionMessages.h"
#include "Protocol/Messages/World/PlayerLifecycleMessages.h"
#include "Protocol/Messages/World/PlayerModifyMessages.h"
#include "Protocol/Messages/World/PlayerQueryMessages.h"
#include "Protocol/Messages/World/PlayerRouteMessages.h"
#include "Servers/App/ServerCallProxy.h"
#include "Servers/World/Player/PlayerInventory.h"

MCLASS(Type=Rpc)
class MGatewayLogin : public MServerCallProxyBase
{
public:
    MGENERATED_BODY(MGatewayLogin, MServerCallProxyBase, 0)
public:
    MFUNCTION(ServerCall, Target=Login)
    MFuture<TResult<FLoginIssueSessionResponse, FAppError>> IssueSession(const FLoginIssueSessionRequest& Request);

private:
    EServerType GetTargetServerType() const override;
};

MCLASS(Type=Rpc)
class MGatewayWorld : public MServerCallProxyBase
{
public:
    MGENERATED_BODY(MGatewayWorld, MServerCallProxyBase, 0)
public:
    MFUNCTION(ServerCall, Target=World)
    MFuture<TResult<FPlayerEnterWorldResponse, FAppError>> PlayerEnterWorld(const FPlayerEnterWorldRequest& Request);

    MFUNCTION(ServerCall, Target=World)
    MFuture<TResult<FPlayerFindResponse, FAppError>> PlayerFind(const FPlayerFindRequest& Request);

    MFUNCTION(ServerCall, Target=World)
    MFuture<TResult<FPlayerLogoutResponse, FAppError>> PlayerLogout(const FPlayerLogoutRequest& Request);

    MFUNCTION(ServerCall, Target=World)
    MFuture<TResult<FPlayerSwitchSceneResponse, FAppError>> PlayerSwitchScene(const FPlayerSwitchSceneRequest& Request);

    MFUNCTION(ServerCall, Target=World)
    MFuture<TResult<FPlayerUpdateRouteResponse, FAppError>> PlayerUpdateRoute(const FPlayerUpdateRouteRequest& Request);

    MFUNCTION(ServerCall, Target=World)
    MFuture<TResult<FPlayerQueryProfileResponse, FAppError>> PlayerQueryProfile(const FPlayerQueryProfileRequest& Request);

    MFUNCTION(ServerCall, Target=World)
    MFuture<TResult<FPlayerQueryInventoryResponse, FAppError>> PlayerQueryInventory(const FPlayerQueryInventoryRequest& Request);

    MFUNCTION(ServerCall, Target=World)
    MFuture<TResult<FPlayerQueryProgressionResponse, FAppError>> PlayerQueryProgression(
        const FPlayerQueryProgressionRequest& Request);

    MFUNCTION(ServerCall, Target=World)
    MFuture<TResult<FPlayerChangeGoldResponse, FAppError>> PlayerChangeGold(const FPlayerChangeGoldRequest& Request);

    MFUNCTION(ServerCall, Target=World)
    MFuture<TResult<FPlayerEquipItemResponse, FAppError>> PlayerEquipItem(const FPlayerEquipItemRequest& Request);

    MFUNCTION(ServerCall, Target=World)
    MFuture<TResult<FPlayerGrantExperienceResponse, FAppError>> PlayerGrantExperience(
        const FPlayerGrantExperienceRequest& Request);

    MFUNCTION(ServerCall, Target=World)
    MFuture<TResult<FPlayerModifyHealthResponse, FAppError>> PlayerModifyHealth(
        const FPlayerModifyHealthRequest& Request);

private:
    EServerType GetTargetServerType() const override;
};

