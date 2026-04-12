#pragma once

#include "Common/Runtime/Object/Object.h"
#include "Common/Runtime/Reflect/Reflection.h"
#include "Protocol/Messages/World/PlayerQueryMessages.h"
#include "Servers/App/ServerCallAsyncSupport.h"
#include "Servers/World/Player/PlayerInventory.h"
#include "Servers/World/Player/PlayerProgression.h"

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

    uint32 ResolveCurrentSceneId() const;

    uint32 ResolveCurrentHealth() const;

    void SyncRuntimeState(uint32 InCurrentSceneId, uint32 InHealth);

    void LoadPersistenceState(
        uint32 InCurrentSceneId,
        uint32 InGold,
        const MString& InEquippedItem,
        uint32 InLevel,
        uint32 InExperience,
        uint32 InHealth);

    MFUNCTION(ServerCall)
    MFuture<TResult<FPlayerQueryProfileResponse, FAppError>> PlayerQueryProfile(
        const FPlayerQueryProfileRequest& Request);

    MPlayerInventory* GetInventory() const;

    MPlayerProgression* GetProgression() const;

    void VisitReferencedObjects(const TFunction<void(MObject*)>& Visitor) const override;

private:
    MPlayerInventory* Inventory = nullptr;
    MPlayerProgression* Progression = nullptr;
};
