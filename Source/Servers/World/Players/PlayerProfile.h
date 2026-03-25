#pragma once

#include "Common/Runtime/Object/Object.h"
#include "Common/Runtime/Reflect/Reflection.h"
#include "Servers/World/Players/PlayerInventory.h"
#include "Servers/World/Players/PlayerProgression.h"

MCLASS(Type=Object)
class MPlayerProfile : public MObject
{
public:
    MGENERATED_BODY(MPlayerProfile, MObject, 0)
public:
    MPlayerProfile();

    MPROPERTY(PersistentData | Replicated)
    uint64 PlayerId = 0;

    // Persistence bridge for the last known scene until scene residency is fully migrated into Pawn snapshots.
    MPROPERTY(PersistentData | Replicated)
    uint32 CurrentSceneId = 1;

    void InitializeForPlayer(uint64 InPlayerId, uint32 InCurrentSceneId);

    void SetCurrentSceneId(uint32 InCurrentSceneId);

    void LoadPersistenceState(
        uint32 InCurrentSceneId,
        uint32 InGold,
        const MString& InEquippedItem,
        uint32 InLevel,
        uint32 InExperience,
        uint32 InHealth);

    MPlayerInventory* GetInventory() const;

    MPlayerProgression* GetProgression() const;

    void VisitReferencedObjects(const TFunction<void(MObject*)>& Visitor) const override;

private:
    MPlayerInventory* Inventory = nullptr;
    MPlayerProgression* Progression = nullptr;
};
