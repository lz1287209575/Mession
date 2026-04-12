#include "Servers/World/Player/PlayerProfile.h"

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

uint32 MPlayerProfile::ResolveCurrentSceneId() const
{
    return CurrentSceneId != 0 ? CurrentSceneId : 1;
}

uint32 MPlayerProfile::ResolveCurrentHealth() const
{
    return Progression ? Progression->Health : 100;
}

void MPlayerProfile::SyncRuntimeState(uint32 InCurrentSceneId, uint32 InHealth)
{
    SetCurrentSceneId(InCurrentSceneId != 0 ? InCurrentSceneId : ResolveCurrentSceneId());
    if (Progression)
    {
        Progression->SetHealth(InHealth);
    }
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

MFuture<TResult<FPlayerQueryProfileResponse, FAppError>> MPlayerProfile::PlayerQueryProfile(
    const FPlayerQueryProfileRequest& /*Request*/)
{
    FPlayerQueryProfileResponse Response;
    Response.PlayerId = PlayerId;
    Response.CurrentSceneId = ResolveCurrentSceneId();

    if (Inventory)
    {
        Response.Gold = Inventory->Gold;
        Response.EquippedItem = Inventory->EquippedItem;
    }

    if (Progression)
    {
        Response.Level = Progression->Level;
        Response.Experience = Progression->Experience;
        Response.Health = Progression->Health;
    }

    return MServerCallAsyncSupport::MakeSuccessFuture(std::move(Response));
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
