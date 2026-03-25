#include "Servers/World/Players/PlayerProfile.h"

MPlayerProfile::MPlayerProfile()
{
    Inventory = CreateDefaultSubObject<MPlayerInventory>(this, "Inventory");
    Progression = CreateDefaultSubObject<MPlayerProgression>(this, "Progression");
}

void MPlayerProfile::InitializeForPlayer(uint64 InPlayerId, uint32 InCurrentSceneId)
{
    PlayerId = InPlayerId;
    CurrentSceneId = InCurrentSceneId;
    MarkPropertyDirty("PlayerId");
    MarkPropertyDirty("CurrentSceneId");
}

void MPlayerProfile::SetCurrentSceneId(uint32 InCurrentSceneId)
{
    CurrentSceneId = InCurrentSceneId;
    MarkPropertyDirty("CurrentSceneId");
}

void MPlayerProfile::LoadPersistenceState(
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
        Inventory->LoadState(InGold, InEquippedItem);
    }
    if (Progression)
    {
        Progression->LoadState(InLevel, InExperience, InHealth);
    }
}

MPlayerInventory* MPlayerProfile::GetInventory() const
{
    return Inventory;
}

MPlayerProgression* MPlayerProfile::GetProgression() const
{
    return Progression;
}

void MPlayerProfile::VisitReferencedObjects(const TFunction<void(MObject*)>& Visitor) const
{
    MObject::VisitReferencedObjects(Visitor);
    if (Visitor)
    {
        Visitor(Inventory);
        Visitor(Progression);
    }
}
