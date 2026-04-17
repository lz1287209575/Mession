#include "Servers/World/Player/PlayerInventory.h"

#include "Servers/World/Player/PlayerProfile.h"

#include <limits>

void MPlayerInventory::SetState(uint32 InGold, const MString& InEquippedItem)
{
    Gold = InGold;
    EquippedItem = InEquippedItem;
    MarkPropertyDirty("Gold");
    MarkPropertyDirty("EquippedItem");
}

void MPlayerInventory::LoadState(uint32 InGold, const MString& InEquippedItem)
{
    Gold = InGold;
    EquippedItem = InEquippedItem;
}

TResult<FPlayerChangeGoldResponse, FAppError> MPlayerInventory::ApplyGoldDelta(int32 DeltaGold)
{
    const int64 NextGold = static_cast<int64>(Gold) + static_cast<int64>(DeltaGold);
    if (NextGold < 0)
    {
        return MakeErrorResult<FPlayerChangeGoldResponse>(FAppError::Make(
            "player_gold_insufficient",
            "PlayerChangeGold"));
    }

    if (NextGold > static_cast<int64>(std::numeric_limits<uint32>::max()))
    {
        return MakeErrorResult<FPlayerChangeGoldResponse>(FAppError::Make(
            "player_gold_overflow",
            "PlayerChangeGold"));
    }

    Gold = static_cast<uint32>(NextGold);
    MarkPropertyDirty("Gold");

    FPlayerChangeGoldResponse Response;
    if (const MPlayerProfile* Profile = dynamic_cast<const MPlayerProfile*>(GetOuter()))
    {
        Response.PlayerId = Profile->PlayerId;
    }
    Response.Gold = Gold;
    return TResult<FPlayerChangeGoldResponse, FAppError>::Ok(std::move(Response));
}

MFuture<TResult<FPlayerQueryInventoryResponse, FAppError>> MPlayerInventory::PlayerQueryInventory(
    const FPlayerQueryInventoryRequest& /*Request*/)
{
    FPlayerQueryInventoryResponse Response;
    if (const MPlayerProfile* Profile = dynamic_cast<const MPlayerProfile*>(GetOuter()))
    {
        Response.PlayerId = Profile->PlayerId;
    }

    Response.Gold = Gold;
    Response.EquippedItem = EquippedItem;
    return MServerCallAsyncSupport::MakeSuccessFuture(std::move(Response));
}

MFuture<TResult<FPlayerChangeGoldResponse, FAppError>> MPlayerInventory::PlayerChangeGold(
    const FPlayerChangeGoldRequest& Request)
{
    return MServerCallAsyncSupport::MakeResultFuture(ApplyGoldDelta(Request.DeltaGold));
}

MFuture<TResult<FPlayerEquipItemResponse, FAppError>> MPlayerInventory::PlayerEquipItem(
    const FPlayerEquipItemRequest& Request)
{
    if (Request.EquippedItem.empty())
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerEquipItemResponse>(
            "equipped_item_required",
            "PlayerEquipItem");
    }

    EquippedItem = Request.EquippedItem;
    MarkPropertyDirty("EquippedItem");

    FPlayerEquipItemResponse Response;
    if (const MPlayerProfile* Profile = dynamic_cast<const MPlayerProfile*>(GetOuter()))
    {
        Response.PlayerId = Profile->PlayerId;
    }
    Response.EquippedItem = EquippedItem;
    return MServerCallAsyncSupport::MakeSuccessFuture(std::move(Response));
}
