#include "Servers/World/Player/PlayerService.h"

#include "Servers/World/Player/PlayerInventory.h"
#include "Servers/World/Player/PlayerPawn.h"
#include "Servers/World/Player/PlayerProfile.h"
#include "Servers/World/Player/PlayerProgression.h"

MFuture<TResult<FPlayerFindResponse, FAppError>> MPlayerService::PlayerFind(const FPlayerFindRequest& Request)
{
    if (Request.PlayerId == 0)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerFindResponse>("player_id_required", "PlayerFind");
    }

    if (MPlayer* Player = FindPlayer(Request.PlayerId))
    {
        return Player->PlayerFind(Request);
    }

    FPlayerFindResponse Response;
    Response.PlayerId = Request.PlayerId;
    Response.bFound = false;
    return MServerCallAsyncSupport::MakeSuccessFuture(std::move(Response));
}

MFuture<TResult<FPlayerQueryProfileResponse, FAppError>> MPlayerService::PlayerQueryProfile(
    const FPlayerQueryProfileRequest& Request)
{
    return MPlayerServiceDetail::DispatchPlayerComponent<MPlayerProfile, FPlayerQueryProfileResponse>(
        this,
        Request,
        &MPlayerService::FindProfile,
        &MPlayerProfile::PlayerQueryProfile,
        "player_profile_missing",
        "PlayerQueryProfile");
}

MFuture<TResult<FPlayerQueryPawnResponse, FAppError>> MPlayerService::PlayerQueryPawn(
    const FPlayerQueryPawnRequest& Request)
{
    return MPlayerServiceDetail::DispatchPlayerComponent<MPlayerPawn, FPlayerQueryPawnResponse>(
        this,
        Request,
        &MPlayerService::FindPawn,
        &MPlayerPawn::PlayerQueryPawn,
        "player_pawn_missing",
        "PlayerQueryPawn");
}

MFuture<TResult<FPlayerQueryInventoryResponse, FAppError>> MPlayerService::PlayerQueryInventory(
    const FPlayerQueryInventoryRequest& Request)
{
    return MPlayerServiceDetail::DispatchPlayerComponent<MPlayerInventory, FPlayerQueryInventoryResponse>(
        this,
        Request,
        &MPlayerService::FindInventory,
        &MPlayerInventory::PlayerQueryInventory,
        "player_inventory_missing",
        "PlayerQueryInventory");
}

MFuture<TResult<FPlayerQueryProgressionResponse, FAppError>> MPlayerService::PlayerQueryProgression(
    const FPlayerQueryProgressionRequest& Request)
{
    return MPlayerServiceDetail::DispatchPlayerComponent<MPlayerProgression, FPlayerQueryProgressionResponse>(
        this,
        Request,
        &MPlayerService::FindProgression,
        &MPlayerProgression::PlayerQueryProgression,
        "player_progression_missing",
        "PlayerQueryProgression");
}
