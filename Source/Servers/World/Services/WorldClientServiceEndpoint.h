#pragma once

#include "Common/Runtime/Reflect/Reflection.h"
#include "Protocol/Messages/Combat/CombatClientMessages.h"
#include "Protocol/Messages/Gateway/GatewayClientMessages.h"
#include "Servers/App/ClientCallAsyncSupport.h"

class MWorldLoginRpc;
class MWorldPlayerServiceEndpoint;
class MWorldServer;

namespace MWorldClientDetail
{
class FClientPlayerCallBinding;
}

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
        MWorldServer* InWorldServer,
        MWorldLoginRpc* InLoginRpc);

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
    friend class MWorldClientFlows::FClientLoginWorkflow;

    MWorldClientDetail::FClientPlayerCallBinding ClientPlayerCall() const;

    MFuture<TResult<FClientLoginResponse, FAppError>> StartClientLoginFlow(
        const FClientLoginRequest& Request,
        uint64 GatewayConnectionId);

    MWorldServer* WorldServer = nullptr;
    MWorldLoginRpc* LoginRpc = nullptr;
};
