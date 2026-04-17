#include "Servers/World/WorldClient.h"
#include "Common/Runtime/Concurrency/FiberAwait.h"
#include "Servers/World/WorldClientCommon.h"
#include "Servers/World/Player/PlayerService.h"
#include "Servers/World/WorldServer.h"

void MWorldClient::Client_CastSkill(FWorldCastSkillRequest& Request, FClientCastSkillResponse& Response)
{
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

    (void)MWorldClientCommon::StartAsyncClientResponse(
        Response,
        WorldServer->GetTaskRunner(),
        "client_cast_skill_failed",
        [PlayerService, Request = FWorldCastSkillRequest(Request)]() mutable
        {
            return MWorldClientCommon::BuildProjectedResult<FClientCastSkillResponse>(
                MAwait(PlayerService->CastSkill(Request)));
        });
}

void MWorldClient::Client_DebugSpawnMonster(
    FWorldSpawnMonsterRequest& Request,
    FClientDebugSpawnMonsterResponse& Response)
{
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

    (void)MWorldClientCommon::StartAsyncClientResponse(
        Response,
        WorldServer->GetTaskRunner(),
        "client_debug_spawn_monster_failed",
        [PlayerService, Request = FWorldSpawnMonsterRequest(Request)]() mutable
        {
            return MWorldClientCommon::BuildProjectedResult<FClientDebugSpawnMonsterResponse>(
                MAwait(PlayerService->SpawnMonster(Request)));
        });
}

void MWorldClient::Client_CastSkillAtUnit(
    FWorldCastSkillAtUnitRequest& Request,
    FClientCastSkillAtUnitResponse& Response)
{
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

    (void)MWorldClientCommon::StartAsyncClientResponse(
        Response,
        WorldServer->GetTaskRunner(),
        "client_cast_skill_at_unit_failed",
        [PlayerService, Request = FWorldCastSkillAtUnitRequest(Request)]() mutable
        {
            return MWorldClientCommon::BuildProjectedResult<FClientCastSkillAtUnitResponse>(
                MAwait(PlayerService->CastSkillAtUnit(Request)));
        });
}
