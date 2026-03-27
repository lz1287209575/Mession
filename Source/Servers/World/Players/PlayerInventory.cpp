#include "Servers/World/Players/PlayerInventory.h"

#include "Servers/World/Players/PlayerProfile.h"

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
