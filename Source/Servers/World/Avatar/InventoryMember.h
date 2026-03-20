#pragma once

#include "Servers/World/Avatar/AvatarMember.h"

MSTRUCT()
struct SInventoryItem
{
    MPROPERTY(Edit | SaveGame | RepToClient)
    uint64 InstanceId = 0;

    MPROPERTY(Edit | SaveGame | RepToClient)
    uint32 ItemId = 0;

    MPROPERTY(Edit | SaveGame | RepToClient)
    uint32 Count = 0;

    MPROPERTY(Edit | SaveGame | RepToClient)
    bool bBound = false;

    MPROPERTY(Edit | SaveGame | RepToClient)
    int64 ExpireAtUnixSeconds = 0;

    MPROPERTY(Edit | SaveGame | RepToClient)
    uint32 Flags = 0;
};

MCLASS()
class MInventoryMember : public MAvatarMember
{
public:
    MGENERATED_BODY(MInventoryMember, MAvatarMember, 0)

public:
    MPROPERTY(Edit | SaveGame | RepToClient)
    int32 Gold = 0;

    MPROPERTY(Edit | SaveGame)
    uint64 OwnerPlayerId = 0;

    MPROPERTY(Edit | SaveGame | RepToClient)
    TVector<SInventoryItem> Items;

    MPROPERTY(Edit | SaveGame)
    uint32 MaxSlots = 64;

    MPROPERTY(Edit | SaveGame)
    uint64 NextItemInstanceId = 1;

    MFUNCTION()
    bool AddItem(uint32 ItemId);

    MFUNCTION()
    bool RemoveItem(uint32 ItemId);

    MFUNCTION()
    void AddGold(int32 Amount);

    MFUNCTION()
    bool SpendGold(int32 Amount);

    void SetOwnerPlayerId(uint64 InOwnerPlayerId) override;

    int32 GetGold() const { return Gold; }
    uint32 GetMaxSlots() const { return MaxSlots; }
    const TVector<SInventoryItem>& GetItems() const { return Items; }
    uint32 GetTotalItemCount() const;
    uint32 GetItemCount(uint32 ItemId) const;
    MString BuildSummary() const;
};
