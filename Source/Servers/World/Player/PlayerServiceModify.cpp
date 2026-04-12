#include "Servers/World/Player/PlayerService.h"

#include "Servers/World/Player/PlayerInventory.h"
#include "Servers/World/Player/PlayerProgression.h"

MFuture<TResult<FPlayerChangeGoldResponse, FAppError>> MPlayerService::PlayerChangeGold(
    const FPlayerChangeGoldRequest& Request)
{
    return MPlayerServiceDetail::DispatchPlayerComponent<MPlayerInventory, FPlayerChangeGoldResponse>(
        this,
        Request,
        &MPlayerService::FindInventory,
        &MPlayerInventory::PlayerChangeGold,
        "player_inventory_missing",
        "PlayerChangeGold");
}

MFuture<TResult<FPlayerEquipItemResponse, FAppError>> MPlayerService::PlayerEquipItem(
    const FPlayerEquipItemRequest& Request)
{
    return MPlayerServiceDetail::DispatchPlayerComponent<MPlayerInventory, FPlayerEquipItemResponse>(
        this,
        Request,
        &MPlayerService::FindInventory,
        &MPlayerInventory::PlayerEquipItem,
        "player_inventory_missing",
        "PlayerEquipItem");
}

MFuture<TResult<FPlayerGrantExperienceResponse, FAppError>> MPlayerService::PlayerGrantExperience(
    const FPlayerGrantExperienceRequest& Request)
{
    return MPlayerServiceDetail::DispatchPlayerComponent<MPlayerProgression, FPlayerGrantExperienceResponse>(
        this,
        Request,
        &MPlayerService::FindProgression,
        &MPlayerProgression::PlayerGrantExperience,
        "player_progression_missing",
        "PlayerGrantExperience");
}

MFuture<TResult<FPlayerModifyHealthResponse, FAppError>> MPlayerService::PlayerModifyHealth(
    const FPlayerModifyHealthRequest& Request)
{
    if (Request.PlayerId == 0)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerModifyHealthResponse>(
            "player_id_required",
            "PlayerModifyHealth");
    }

    MPlayerProgression* Progression = FindProgression(Request.PlayerId);
    if (!Progression)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerModifyHealthResponse>(
            "player_progression_missing",
            "PlayerModifyHealth");
    }

    MFuture<TResult<FPlayerModifyHealthResponse, FAppError>> Future = Progression->PlayerModifyHealth(Request);

    Future.Then([this, PlayerId = Request.PlayerId](MFuture<TResult<FPlayerModifyHealthResponse, FAppError>> Completed)
    {
        try
        {
            const TResult<FPlayerModifyHealthResponse, FAppError> Result = Completed.Get();
            if (Result.IsOk())
            {
                QueueScenePlayerUpdateNotify(PlayerId);
            }
        }
        catch (...)
        {
        }
    });

    return Future;
}
