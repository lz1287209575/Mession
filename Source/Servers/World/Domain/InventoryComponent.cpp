#include "Servers/World/Domain/InventoryComponent.h"

void MInventoryComponent::SetInventoryState(uint32 InGold, const MString& InEquippedItem)
{
    Gold = InGold;
    EquippedItem = InEquippedItem;
    MarkPropertyDirty("Gold");
    MarkPropertyDirty("EquippedItem");
}

void MInventoryComponent::LoadInventoryState(uint32 InGold, const MString& InEquippedItem)
{
    Gold = InGold;
    EquippedItem = InEquippedItem;
}
