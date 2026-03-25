#include "Servers/World/Players/PlayerInventory.h"

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
