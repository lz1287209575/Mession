#pragma once

#include "Common/Runtime/Object/Object.h"
#include "Common/Runtime/Reflect/Reflection.h"
#include "Servers/World/Domain/AttributeComponent.h"
#include "Servers/World/Domain/InventoryComponent.h"

MCLASS(Type=Object)
class MPlayerAvatar : public MObject
{
public:
    MGENERATED_BODY(MPlayerAvatar, MObject, 0)
public:
    MPlayerAvatar();

    MPROPERTY(PersistentData | Replicated)
    uint64 PlayerId = 0;

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

    MInventoryComponent* GetInventory() const;

    MAttributeComponent* GetAttributes() const;

    void VisitReferencedObjects(const TFunction<void(MObject*)>& Visitor) const override;

private:
    MInventoryComponent* Inventory = nullptr;
    MAttributeComponent* Attributes = nullptr;
};
