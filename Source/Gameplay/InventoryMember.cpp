#include "Gameplay/InventoryMember.h"
#include "Build/Generated/MInventoryMember.mgenerated.h"

#include <algorithm>

void MInventoryMember::SetOwnerPlayerId(uint64 InOwnerPlayerId)
{
    MAvatarMember::SetOwnerPlayerId(InOwnerPlayerId);
    if (OwnerPlayerId == InOwnerPlayerId)
    {
        return;
    }
    OwnerPlayerId = InOwnerPlayerId;
    SetPropertyDirty(Prop_MInventoryMember_OwnerPlayerId());
}

bool MInventoryMember::AddItem(uint32 ItemId)
{
    if (ItemId == 0)
    {
        return false;
    }

    auto It = std::find_if(Items.begin(), Items.end(), [ItemId](const SInventoryItem& Item)
    {
        return Item.ItemId == ItemId;
    });
    if (It != Items.end())
    {
        ++It->Count;
        SetPropertyDirty(Prop_MInventoryMember_Items());
        return true;
    }

    if (Items.size() >= static_cast<size_t>(MaxSlots))
    {
        return false;
    }

    SInventoryItem NewItem;
    NewItem.InstanceId = NextItemInstanceId++;
    if (NewItem.InstanceId == 0)
    {
        NewItem.InstanceId = NextItemInstanceId++;
    }
    NewItem.ItemId = ItemId;
    NewItem.Count = 1;
    Items.push_back(NewItem);
    SetPropertyDirty(Prop_MInventoryMember_Items());
    SetPropertyDirty(Prop_MInventoryMember_NextItemInstanceId());
    return true;
}

bool MInventoryMember::RemoveItem(uint32 ItemId)
{
    if (ItemId == 0)
    {
        return false;
    }

    auto It = std::find_if(Items.begin(), Items.end(), [ItemId](const SInventoryItem& Item)
    {
        return Item.ItemId == ItemId;
    });
    if (It == Items.end())
    {
        return false;
    }

    if (It->Count > 1)
    {
        --It->Count;
    }
    else
    {
        Items.erase(It);
    }
    SetPropertyDirty(Prop_MInventoryMember_Items());
    return true;
}

void MInventoryMember::AddGold(int32 Amount)
{
    if (Amount <= 0)
    {
        return;
    }

    Gold += Amount;
    SetPropertyDirty(Prop_MInventoryMember_Gold());
}

bool MInventoryMember::SpendGold(int32 Amount)
{
    if (Amount <= 0)
    {
        return true;
    }
    if (Gold < Amount)
    {
        return false;
    }

    Gold -= Amount;
    SetPropertyDirty(Prop_MInventoryMember_Gold());
    return true;
}

uint32 MInventoryMember::GetTotalItemCount() const
{
    uint32 Total = 0;
    for (const SInventoryItem& Item : Items)
    {
        Total += Item.Count;
    }
    return Total;
}

uint32 MInventoryMember::GetItemCount(uint32 ItemId) const
{
    if (ItemId == 0)
    {
        return 0;
    }
    for (const SInventoryItem& Item : Items)
    {
        if (Item.ItemId == ItemId)
        {
            return Item.Count;
        }
    }
    return 0;
}

MString MInventoryMember::BuildSummary() const
{
    return "gold=" + MString::ToString(Gold) +
           ", slots=" + MString::ToString(static_cast<uint64>(Items.size())) + "/" + MString::ToString(static_cast<uint64>(MaxSlots)) +
           ", items=" + MString::ToString(static_cast<uint64>(GetTotalItemCount()));
}
