#pragma once

#include "Common/Runtime/Reflect/Reflection.h"
#include "Protocol/Messages/Combat/CombatClientMessages.h"
#include "Protocol/Messages/Combat/CombatWorldMessages.h"
#include "Protocol/Messages/Gateway/GatewayLoginMessages.h"
#include "Protocol/Messages/Gateway/GatewayPlayerLifecycleMessages.h"
#include "Protocol/Messages/Gateway/GatewayPlayerModifyMessages.h"
#include "Protocol/Messages/Gateway/GatewayPlayerMovementMessages.h"
#include "Protocol/Messages/Gateway/GatewayPlayerQueryMessages.h"
#include "Protocol/Messages/Gateway/GatewayPlayerSocialMessages.h"
#include "Protocol/Messages/World/PlayerLifecycleMessages.h"
#include "Protocol/Messages/World/PlayerModifyMessages.h"
#include "Protocol/Messages/World/PlayerMovementMessages.h"
#include "Protocol/Messages/World/PlayerQueryMessages.h"
#include "Protocol/Messages/World/PlayerRouteMessages.h"
#include "Protocol/Messages/World/PlayerSocialMessages.h"
#include "Servers/World/Player/PlayerCombatProfile.h"
#include "Servers/World/Player/PlayerInventory.h"
#include "Servers/App/ClientCallAsyncSupport.h"

class MWorldLogin;
class MWorldServer;

namespace MWorldClientPlayer
{
class FRequest;
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
    void Client_FindPlayer(FPlayerFindRequest& Request, FClientFindPlayerResponse& Response);

    MFUNCTION(ClientCall, Target=World)
    void Client_Move(FPlayerMoveRequest& Request, FClientMoveResponse& Response);

    MFUNCTION(ClientCall, Target=World)
    void Client_QueryProfile(FPlayerQueryProfileRequest& Request, FClientQueryProfileResponse& Response);

    MFUNCTION(ClientCall, Target=World)
    void Client_QueryPawn(FPlayerQueryPawnRequest& Request, FClientQueryPawnResponse& Response);

    MFUNCTION(ClientCall, Target=World)
    void Client_QueryInventory(FPlayerQueryInventoryRequest& Request, FClientQueryInventoryResponse& Response);

    MFUNCTION(ClientCall, Target=World)
    void Client_QueryProgression(FPlayerQueryProgressionRequest& Request, FClientQueryProgressionResponse& Response);

    MFUNCTION(ClientCall, Target=World)
    void Client_QueryCombatProfile(FPlayerQueryCombatProfileRequest& Request, FClientQueryCombatProfileResponse& Response);

    MFUNCTION(ClientCall, Target=World)
    void Client_SetPrimarySkill(FPlayerSetPrimarySkillRequest& Request, FClientSetPrimarySkillResponse& Response);

    MFUNCTION(ClientCall, Target=World)
    void Client_Logout(FPlayerLogoutRequest& Request, FClientLogoutResponse& Response);

    MFUNCTION(ClientCall, Target=World)
    void Client_SwitchScene(FPlayerSwitchSceneRequest& Request, FClientSwitchSceneResponse& Response);

    MFUNCTION(ClientCall, Target=World)
    void Client_ChangeGold(FPlayerChangeGoldRequest& Request, FClientChangeGoldResponse& Response);

    MFUNCTION(ClientCall, Target=World)
    void Client_EquipItem(FPlayerEquipItemRequest& Request, FClientEquipItemResponse& Response);

    MFUNCTION(ClientCall, Target=World)
    void Client_GrantExperience(FPlayerGrantExperienceRequest& Request, FClientGrantExperienceResponse& Response);

    MFUNCTION(ClientCall, Target=World)
    void Client_ModifyHealth(FPlayerModifyHealthRequest& Request, FClientModifyHealthResponse& Response);

    MFUNCTION(ClientCall, Target=World)
    void Client_OpenTradeSession(FPlayerOpenTradeSessionRequest& Request, FClientOpenTradeSessionResponse& Response);

    MFUNCTION(ClientCall, Target=World)
    void Client_ConfirmTrade(FPlayerConfirmTradeRequest& Request, FClientConfirmTradeResponse& Response);

    MFUNCTION(ClientCall, Target=World)
    void Client_CreateParty(FPlayerCreatePartyRequest& Request, FClientCreatePartyResponse& Response);

    MFUNCTION(ClientCall, Target=World)
    void Client_InviteParty(FPlayerInvitePartyRequest& Request, FClientInvitePartyResponse& Response);

    MFUNCTION(ClientCall, Target=World)
    void Client_AcceptPartyInvite(FPlayerAcceptPartyInviteRequest& Request, FClientAcceptPartyInviteResponse& Response);

    MFUNCTION(ClientCall, Target=World)
    void Client_KickPartyMember(FPlayerKickPartyMemberRequest& Request, FClientKickPartyMemberResponse& Response);

    MFUNCTION(ClientCall, Target=World)
    void Client_CastSkill(FWorldCastSkillRequest& Request, FClientCastSkillResponse& Response);

    MFUNCTION(ClientCall, Target=World)
    void Client_DebugSpawnMonster(FWorldSpawnMonsterRequest& Request, FClientDebugSpawnMonsterResponse& Response);

    MFUNCTION(ClientCall, Target=World)
    void Client_CastSkillAtUnit(FWorldCastSkillAtUnitRequest& Request, FClientCastSkillAtUnitResponse& Response);

private:
    MWorldClientPlayer::FRequest PlayerRequest() const;

    TResult<FClientLoginResponse, FAppError> DoClientLogin(
        FClientLoginRequest Request,
        uint64 GatewayConnectionId);

    MWorldServer* WorldServer = nullptr;
    MWorldLogin* Login = nullptr;
};
