#include "Servers/World/Domain/PlayerAvatar.h"

MPlayerAvatar::MPlayerAvatar()
{
    Inventory = CreateDefaultSubObject<MInventoryComponent>(this, "Inventory");
    Attributes = CreateDefaultSubObject<MAttributeComponent>(this, "Attributes");
}

void MPlayerAvatar::InitializeForPlayer(uint64 InPlayerId, uint32 InCurrentSceneId)
{
    PlayerId = InPlayerId;
    CurrentSceneId = InCurrentSceneId;
    MarkPropertyDirty("PlayerId");
    MarkPropertyDirty("CurrentSceneId");
}

void MPlayerAvatar::SetCurrentSceneId(uint32 InCurrentSceneId)
{
    CurrentSceneId = InCurrentSceneId;
    MarkPropertyDirty("CurrentSceneId");
}

void MPlayerAvatar::LoadPersistenceState(
    uint32 InCurrentSceneId,
    uint32 InGold,
    const MString& InEquippedItem,
    uint32 InLevel,
    uint32 InExperience,
    uint32 InHealth)
{
    CurrentSceneId = InCurrentSceneId;
    if (Inventory)
    {
        Inventory->LoadInventoryState(InGold, InEquippedItem);
    }
    if (Attributes)
    {
        Attributes->LoadProgression(InLevel, InExperience, InHealth);
    }
}

MInventoryComponent* MPlayerAvatar::GetInventory() const
{
    return Inventory;
}

MAttributeComponent* MPlayerAvatar::GetAttributes() const
{
    return Attributes;
}

void MPlayerAvatar::VisitReferencedObjects(const TFunction<void(MObject*)>& Visitor) const
{
    MObject::VisitReferencedObjects(Visitor);
    if (Visitor)
    {
        Visitor(Inventory);
        Visitor(Attributes);
    }
}
