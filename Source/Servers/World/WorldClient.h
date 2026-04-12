#pragma once

#include "Common/Runtime/Reflect/Reflection.h"
#include "Protocol/Messages/Combat/CombatClientMessages.h"
#include "Protocol/Messages/Gateway/GatewayLoginMessages.h"
#include "Protocol/Messages/Gateway/GatewayPlayerLifecycleMessages.h"
#include "Protocol/Messages/Gateway/GatewayPlayerModifyMessages.h"
#include "Protocol/Messages/Gateway/GatewayPlayerMovementMessages.h"
#include "Protocol/Messages/Gateway/GatewayPlayerQueryMessages.h"
#include "Servers/App/ClientCallAsyncSupport.h"

class MWorldLogin;
class MWorldServer;

namespace MWorldClientPlayer
{
class FRequest;
}

namespace MWorldClientLogin
{
class FAction;
}

MCLASS(Type=Service)
class MWorldClient : public MObject
{
public:
    MGENERATED_BODY(MWorldClient, MObject, 0)
public:
    void Initialize(
        MWorldServer* InWorldServer,
        MWorldLogin* InLogin);

    MFUNCTION(ClientCall, Target=World)
    void Client_Login(FClientLoginRequest& Request, FClientLoginResponse& Response);

    MFUNCTION(ClientCall, Target=World)
    void Client_FindPlayer(FClientFindPlayerRequest& Request, FClientFindPlayerResponse& Response);

    MFUNCTION(ClientCall, Target=World)
    void Client_Move(FClientMoveRequest& Request, FClientMoveResponse& Response);

    MFUNCTION(ClientCall, Target=World)
    void Client_QueryProfile(FClientQueryProfileRequest& Request, FClientQueryProfileResponse& Response);

    MFUNCTION(ClientCall, Target=World)
    void Client_QueryPawn(FClientQueryPawnRequest& Request, FClientQueryPawnResponse& Response);

    MFUNCTION(ClientCall, Target=World)
    void Client_QueryInventory(FClientQueryInventoryRequest& Request, FClientQueryInventoryResponse& Response);

    MFUNCTION(ClientCall, Target=World)
    void Client_QueryProgression(FClientQueryProgressionRequest& Request, FClientQueryProgressionResponse& Response);

    MFUNCTION(ClientCall, Target=World)
    void Client_Logout(FClientLogoutRequest& Request, FClientLogoutResponse& Response);

    MFUNCTION(ClientCall, Target=World)
    void Client_SwitchScene(FClientSwitchSceneRequest& Request, FClientSwitchSceneResponse& Response);

    MFUNCTION(ClientCall, Target=World)
    void Client_ChangeGold(FClientChangeGoldRequest& Request, FClientChangeGoldResponse& Response);

    MFUNCTION(ClientCall, Target=World)
    void Client_EquipItem(FClientEquipItemRequest& Request, FClientEquipItemResponse& Response);

    MFUNCTION(ClientCall, Target=World)
    void Client_GrantExperience(FClientGrantExperienceRequest& Request, FClientGrantExperienceResponse& Response);

    MFUNCTION(ClientCall, Target=World)
    void Client_ModifyHealth(FClientModifyHealthRequest& Request, FClientModifyHealthResponse& Response);

    MFUNCTION(ClientCall, Target=World)
    void Client_CastSkill(FClientCastSkillRequest& Request, FClientCastSkillResponse& Response);

private:
    friend class MWorldClientLogin::FAction;

    MWorldClientPlayer::FRequest PlayerRequest() const;

    MFuture<TResult<FClientLoginResponse, FAppError>> StartClientLogin(
        const FClientLoginRequest& Request,
        uint64 GatewayConnectionId);

    MWorldServer* WorldServer = nullptr;
    MWorldLogin* Login = nullptr;
};

