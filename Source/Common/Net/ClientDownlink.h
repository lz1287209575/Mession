#pragma once

#include "Common/Runtime/Object/Object.h"
#include "Protocol/Messages/Gateway/GatewayPlayerSocialMessages.h"
#include "Protocol/Messages/Scene/SceneSyncMessages.h"
#include "Protocol/Messages/World/WorldInventoryMessages.h"

MCLASS(Type=Object)
class MClientDownlink : public MObject
{
public:
    MGENERATED_BODY(MClientDownlink, MObject, 0)

    MFUNCTION()
    void Client_OnLoginResponse(uint32, uint64) {}

    MFUNCTION()
    void Client_OnActorCreate(uint64, const TByteArray&) {}

    MFUNCTION()
    void Client_OnActorUpdate(uint64, const TByteArray&) {}

    MFUNCTION()
    void Client_OnActorDestroy(uint64) {}

    MFUNCTION()
    void Client_OnObjectCreate(uint64, const TByteArray&) {}

    MFUNCTION()
    void Client_OnObjectUpdate(uint64, const TByteArray&) {}

    MFUNCTION()
    void Client_OnObjectDestroy(uint64) {}

    MFUNCTION()
    void Client_OnInventoryPull(
        uint64,
        int32,
        uint32,
        const TVector<SInventoryItemPayload>&) {}

    MFUNCTION()
    void Client_ScenePlayerEnter(const SPlayerSceneStateMessage&) {}

    MFUNCTION()
    void Client_ScenePlayerUpdate(const SPlayerSceneStateMessage&) {}

    MFUNCTION()
    void Client_ScenePlayerLeave(const SPlayerSceneLeaveMessage&) {}

    MFUNCTION()
    void Client_TradeSessionOpened(const FClientTradeSessionOpenedNotify&) {}

    MFUNCTION()
    void Client_TradeSessionUpdated(const FClientTradeSessionUpdatedNotify&) {}

    MFUNCTION()
    void Client_TradeSessionClosed(const FClientTradeSessionClosedNotify&) {}

    MFUNCTION()
    void Client_PartyCreated(const FClientPartyCreatedNotify&) {}

    MFUNCTION()
    void Client_PartyInviteReceived(const FClientPartyInviteReceivedNotify&) {}

    MFUNCTION()
    void Client_PartyMemberJoined(const FClientPartyMemberJoinedNotify&) {}

    MFUNCTION()
    void Client_PartyMemberRemoved(const FClientPartyMemberRemovedNotify&) {}

    MFUNCTION()
    void Client_PartyDisbanded(const FClientPartyDisbandedNotify&) {}
};
