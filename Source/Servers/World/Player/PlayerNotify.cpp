#include "Servers/World/Player/PlayerService.h"

#include "Common/Net/Rpc/RpcPayload.h"
#include "Protocol/Messages/Gateway/GatewayPlayerSocialMessages.h"
#include "Protocol/Messages/Scene/SceneSyncMessages.h"
#include "Servers/World/Player/Player.h"

namespace
{
uint16 ResolveClientNotifyFunctionId(const char* FunctionName)
{
    return FunctionName ? MGET_STABLE_RPC_FUNCTION_ID("MClientDownlink", FunctionName) : 0;
}
}

void MPlayerService::QueueClientNotifyToPlayer(uint64 PlayerId, uint16 FunctionId, const TByteArray& Payload) const
{
    if (PlayerManager)
    {
        PlayerManager->QueueClientNotifyToPlayer(PlayerId, FunctionId, Payload);
    }
}

void MPlayerService::QueueScenePlayerEnterNotify(uint64 PlayerId)
{
    MPlayer* SubjectPlayer = FindPlayer(PlayerId);
    if (!SubjectPlayer || !SubjectPlayer->CanReceiveSceneNotify())
    {
        return;
    }

    SPlayerSceneStateMessage SubjectState;
    if (!SubjectPlayer->TryBuildSceneState(SubjectState))
    {
        return;
    }

    const uint16 EnterFunctionId = MGET_STABLE_RPC_FUNCTION_ID("MClientDownlink", "Client_ScenePlayerEnter");
    if (EnterFunctionId == 0)
    {
        return;
    }

    const uint64 SubjectGatewayConnectionId = SubjectPlayer->ResolveGatewayConnectionId();
    const TByteArray SubjectPayload = BuildPayload(SubjectState);
    if (!PlayerManager)
    {
        return;
    }

    PlayerManager->VisitNotifiablePlayersInScene(
        SubjectState.SceneId,
        [this, PlayerId, SubjectState, EnterFunctionId, SubjectGatewayConnectionId, &SubjectPayload](
            uint64 OtherPlayerId,
            MPlayer* OtherPlayer)
        {
            if (OtherPlayerId == PlayerId || !OtherPlayer)
            {
                return;
            }

            SPlayerSceneStateMessage OtherState;
            if (!OtherPlayer->TryBuildSceneState(OtherState))
            {
                return;
            }

            QueueClientNotifyToPlayer(OtherPlayerId, EnterFunctionId, SubjectPayload);
            if (SubjectGatewayConnectionId != 0)
            {
                WorldServer->QueueClientNotify(SubjectGatewayConnectionId, EnterFunctionId, BuildPayload(OtherState));
            }
        });
}

void MPlayerService::QueueScenePlayerUpdateNotify(uint64 PlayerId)
{
    MPlayer* SubjectPlayer = FindPlayer(PlayerId);
    if (!SubjectPlayer)
    {
        return;
    }

    SPlayerSceneStateMessage SubjectState;
    if (!SubjectPlayer->TryBuildSceneState(SubjectState))
    {
        return;
    }

    const uint16 UpdateFunctionId = MGET_STABLE_RPC_FUNCTION_ID("MClientDownlink", "Client_ScenePlayerUpdate");
    if (UpdateFunctionId == 0)
    {
        return;
    }

    const TByteArray Payload = BuildPayload(SubjectState);
    if (!PlayerManager)
    {
        return;
    }

    PlayerManager->VisitNotifiablePlayersInScene(
        SubjectState.SceneId,
        [this, PlayerId, UpdateFunctionId, &Payload](uint64 OtherPlayerId, MPlayer* OtherPlayer)
        {
            if (OtherPlayerId == PlayerId || !OtherPlayer)
            {
                return;
            }

            QueueClientNotifyToPlayer(OtherPlayerId, UpdateFunctionId, Payload);
        });
}

void MPlayerService::QueueScenePlayerLeaveNotify(uint64 PlayerId, uint32 SceneId)
{
    if (SceneId == 0)
    {
        return;
    }

    const uint16 LeaveFunctionId = MGET_STABLE_RPC_FUNCTION_ID("MClientDownlink", "Client_ScenePlayerLeave");
    if (LeaveFunctionId == 0)
    {
        return;
    }

    SPlayerSceneLeaveMessage LeaveMessage;
    LeaveMessage.PlayerId = PlayerId;
    LeaveMessage.SceneId = static_cast<uint16>(SceneId);

    const TByteArray Payload = BuildPayload(LeaveMessage);
    if (!PlayerManager)
    {
        return;
    }

    PlayerManager->VisitNotifiablePlayersInScene(
        SceneId,
        [this, PlayerId, LeaveFunctionId, &Payload](uint64 OtherPlayerId, MPlayer* OtherPlayer)
        {
            if (OtherPlayerId == PlayerId || !OtherPlayer)
            {
                return;
            }

            QueueClientNotifyToPlayer(OtherPlayerId, LeaveFunctionId, Payload);
        });
}

void MPlayerService::QueueTradeSessionOpenedNotify(const STradeSessionState& Session)
{
    const uint16 FunctionId = ResolveClientNotifyFunctionId("Client_TradeSessionOpened");
    if (FunctionId == 0)
    {
        return;
    }

    FClientTradeSessionOpenedNotify Notify;
    Notify.TradeSessionId = Session.TradeSessionId;
    Notify.InitiatorPlayerId = Session.InitiatorPlayerId;
    Notify.TargetPlayerId = Session.TargetPlayerId;
    Notify.WitnessPlayerId = Session.WitnessPlayerId;
    const TByteArray Payload = BuildPayload(Notify);
    if (!PlayerManager)
    {
        return;
    }

    PlayerManager->QueueClientNotifyToPlayers(
        {
            Session.InitiatorPlayerId,
            Session.TargetPlayerId,
            Session.WitnessPlayerId,
        },
        FunctionId,
        Payload);
}

void MPlayerService::QueueTradeSessionUpdatedNotify(
    const STradeSessionState& Session,
    uint64 ActorPlayerId,
    uint32 ConfirmedCount,
    bool bAllConfirmed)
{
    const uint16 FunctionId = ResolveClientNotifyFunctionId("Client_TradeSessionUpdated");
    if (FunctionId == 0)
    {
        return;
    }

    FClientTradeSessionUpdatedNotify Notify;
    Notify.TradeSessionId = Session.TradeSessionId;
    Notify.ActorPlayerId = ActorPlayerId;
    Notify.ConfirmedCount = ConfirmedCount;
    Notify.bAllConfirmed = bAllConfirmed;
    const TByteArray Payload = BuildPayload(Notify);
    if (!PlayerManager)
    {
        return;
    }

    PlayerManager->QueueClientNotifyToPlayers(
        {
            Session.InitiatorPlayerId,
            Session.TargetPlayerId,
            Session.WitnessPlayerId,
        },
        FunctionId,
        Payload);
}

void MPlayerService::QueueTradeSessionClosedNotify(
    const STradeSessionState& Session,
    uint64 ActorPlayerId,
    const char* Reason)
{
    const uint16 FunctionId = ResolveClientNotifyFunctionId("Client_TradeSessionClosed");
    if (FunctionId == 0)
    {
        return;
    }

    FClientTradeSessionClosedNotify Notify;
    Notify.TradeSessionId = Session.TradeSessionId;
    Notify.ActorPlayerId = ActorPlayerId;
    Notify.Reason = Reason ? Reason : "";
    const TByteArray Payload = BuildPayload(Notify);
    if (!PlayerManager)
    {
        return;
    }

    PlayerManager->QueueClientNotifyToPlayers(
        {
            Session.InitiatorPlayerId,
            Session.TargetPlayerId,
            Session.WitnessPlayerId,
        },
        FunctionId,
        Payload);
}

void MPlayerService::QueuePartyCreatedNotify(const SPartyState& Party)
{
    const uint16 FunctionId = ResolveClientNotifyFunctionId("Client_PartyCreated");
    if (FunctionId == 0)
    {
        return;
    }

    FClientPartyCreatedNotify Notify;
    Notify.PartyId = Party.PartyId;
    Notify.LeaderPlayerId = Party.LeaderPlayerId;
    Notify.MemberPlayerIds = Party.MemberPlayerIds;
    QueueClientNotifyToPlayer(Party.LeaderPlayerId, FunctionId, BuildPayload(Notify));
}

void MPlayerService::QueuePartyInviteNotify(const SPartyState& Party, uint64 TargetPlayerId)
{
    const uint16 FunctionId = ResolveClientNotifyFunctionId("Client_PartyInviteReceived");
    if (FunctionId == 0)
    {
        return;
    }

    FClientPartyInviteReceivedNotify Notify;
    Notify.PartyId = Party.PartyId;
    Notify.LeaderPlayerId = Party.LeaderPlayerId;
    Notify.TargetPlayerId = TargetPlayerId;
    QueueClientNotifyToPlayer(TargetPlayerId, FunctionId, BuildPayload(Notify));
}

void MPlayerService::QueuePartyMemberJoinedNotify(const SPartyState& Party, uint64 JoinedPlayerId)
{
    const uint16 FunctionId = ResolveClientNotifyFunctionId("Client_PartyMemberJoined");
    if (FunctionId == 0)
    {
        return;
    }

    FClientPartyMemberJoinedNotify Notify;
    Notify.PartyId = Party.PartyId;
    Notify.LeaderPlayerId = Party.LeaderPlayerId;
    Notify.JoinedPlayerId = JoinedPlayerId;
    Notify.MemberPlayerIds = Party.MemberPlayerIds;
    const TByteArray Payload = BuildPayload(Notify);
    if (!PlayerManager)
    {
        return;
    }

    PlayerManager->QueueClientNotifyToPlayers(Party.MemberPlayerIds, FunctionId, Payload);
}

void MPlayerService::QueuePartyMemberRemovedNotify(
    const SPartyState& Party,
    uint64 RemovedPlayerId,
    const char* Reason)
{
    const uint16 FunctionId = ResolveClientNotifyFunctionId("Client_PartyMemberRemoved");
    if (FunctionId == 0)
    {
        return;
    }

    FClientPartyMemberRemovedNotify Notify;
    Notify.PartyId = Party.PartyId;
    Notify.LeaderPlayerId = Party.LeaderPlayerId;
    Notify.RemovedPlayerId = RemovedPlayerId;
    Notify.MemberPlayerIds = Party.MemberPlayerIds;
    Notify.Reason = Reason ? Reason : "";
    const TByteArray Payload = BuildPayload(Notify);
    if (!PlayerManager)
    {
        return;
    }

    PlayerManager->QueueClientNotifyToPlayers(Party.MemberPlayerIds, FunctionId, Payload);
    PlayerManager->QueueClientNotifyToPlayer(RemovedPlayerId, FunctionId, Payload);
}

void MPlayerService::QueuePartyDisbandedNotify(const SPartyState& Party, uint64 ActorPlayerId, const char* Reason)
{
    const uint16 FunctionId = ResolveClientNotifyFunctionId("Client_PartyDisbanded");
    if (FunctionId == 0)
    {
        return;
    }

    FClientPartyDisbandedNotify Notify;
    Notify.PartyId = Party.PartyId;
    Notify.LeaderPlayerId = Party.LeaderPlayerId;
    Notify.ActorPlayerId = ActorPlayerId;
    Notify.MemberPlayerIds = Party.MemberPlayerIds;
    Notify.Reason = Reason ? Reason : "";
    const TByteArray Payload = BuildPayload(Notify);
    if (!PlayerManager)
    {
        return;
    }

    PlayerManager->QueueClientNotifyToPlayers(Party.MemberPlayerIds, FunctionId, Payload);
}
