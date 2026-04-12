#include "Servers/World/WorldClient.h"
#include "Servers/World/WorldClientCommon.h"
#include "Servers/World/Player/PlayerService.h"
#include "Servers/World/WorldServer.h"

void MWorldClient::Client_CastSkill(FClientCastSkillRequest& Request, FClientCastSkillResponse& Response)
{
    if (Request.PlayerId == 0 || Request.TargetPlayerId == 0)
    {
        Response.Error = "combat_participants_required";
        return;
    }

    if (!WorldServer)
    {
        Response.Error = "world_server_missing";
        return;
    }

    MPlayerService* PlayerService = WorldServer->GetPlayerService();
    if (!PlayerService)
    {
        Response.Error = "player_service_missing";
        return;
    }

    const SClientCallContext Context = CaptureCurrentClientCallContext();
    if (!Context.IsValid())
    {
        Response.Error = "client_call_context_missing";
        return;
    }

    FWorldCastSkillRequest WorldRequest;
    WorldRequest.PlayerId = Request.PlayerId;
    WorldRequest.TargetPlayerId = Request.TargetPlayerId;
    WorldRequest.SkillId = Request.SkillId;

    (void)MClientCallAsyncSupport::StartDeferred<FClientCastSkillResponse>(
        Context,
        MClientCallAsyncSupport::Map(
            PlayerService->CastSkill(WorldRequest),
            [](const FWorldCastSkillResponse& WorldResponse)
            {
                FClientCastSkillResponse ClientResponse;
                ClientResponse.bSuccess = true;
                ClientResponse.PlayerId = WorldResponse.PlayerId;
                ClientResponse.TargetPlayerId = WorldResponse.TargetPlayerId;
                ClientResponse.SkillId = WorldResponse.SkillId;
                ClientResponse.SceneId = WorldResponse.SceneId;
                ClientResponse.AppliedDamage = WorldResponse.AppliedDamage;
                ClientResponse.TargetHealth = WorldResponse.TargetHealth;
                return ClientResponse;
            }),
        [](const FAppError& Error)
        {
            return MWorldClientCommon::BuildFailureResponse<FClientCastSkillResponse>(Error, "client_cast_skill_failed");
        });
}
