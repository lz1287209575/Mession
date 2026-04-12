#include "Servers/World/Player/PlayerService.h"

#include "Common/Net/Rpc/RpcPayload.h"
#include "Protocol/Messages/Scene/SceneSyncMessages.h"
#include "Servers/World/Player/Player.h"

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
    for (const auto& [OtherPlayerId, OtherPlayer] : GetOnlinePlayers())
    {
        if (OtherPlayerId == PlayerId || !OtherPlayer->CanReceiveSceneNotify())
        {
            continue;
        }

        SPlayerSceneStateMessage OtherState;
        if (!OtherPlayer->TryBuildSceneState(OtherState))
        {
            continue;
        }

        if (OtherState.SceneId != SubjectState.SceneId)
        {
            continue;
        }

        WorldServer->QueueClientNotify(OtherPlayer->ResolveGatewayConnectionId(), EnterFunctionId, SubjectPayload);
        WorldServer->QueueClientNotify(SubjectGatewayConnectionId, EnterFunctionId, BuildPayload(OtherState));
    }
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
    for (const auto& [OtherPlayerId, OtherPlayer] : GetOnlinePlayers())
    {
        if (OtherPlayerId == PlayerId || !OtherPlayer->CanReceiveSceneNotify())
        {
            continue;
        }

        if (OtherPlayer->ResolveCurrentSceneId() != SubjectState.SceneId)
        {
            continue;
        }

        WorldServer->QueueClientNotify(OtherPlayer->ResolveGatewayConnectionId(), UpdateFunctionId, Payload);
    }
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
    for (const auto& [OtherPlayerId, OtherPlayer] : GetOnlinePlayers())
    {
        if (OtherPlayerId == PlayerId || !OtherPlayer->CanReceiveSceneNotify())
        {
            continue;
        }

        if (OtherPlayer->ResolveCurrentSceneId() != SceneId)
        {
            continue;
        }

        WorldServer->QueueClientNotify(OtherPlayer->ResolveGatewayConnectionId(), LeaveFunctionId, Payload);
    }
}
