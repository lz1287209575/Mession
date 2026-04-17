#pragma once

#include "Common/Runtime/Object/Object.h"
#include "Common/Runtime/Reflect/Reflection.h"
#include "Protocol/Messages/World/PlayerModifyMessages.h"
#include "Servers/App/ServerCallRequestValidation.h"
#include "Servers/App/ServerCallAsyncSupport.h"

MSTRUCT()
struct FPlayerQueryInventoryRequest
{
    MPROPERTY(Meta=(NonZero, ErrorCode="player_id_required", ErrorContext="PlayerQueryInventory"))
    uint64 PlayerId = 0;
};

MSTRUCT()
struct FPlayerQueryInventoryResponse
{
    MPROPERTY()
    uint64 PlayerId = 0;

    MPROPERTY()
    uint32 Gold = 0;

    MPROPERTY()
    MString EquippedItem;
};

MCLASS(Type=Object)
class MPlayerInventory : public MObject
{
public:
    MGENERATED_BODY(MPlayerInventory, MObject, 0)
public:
    MPROPERTY(PersistentData | Replicated)
    uint32 Gold = 0;

    MPROPERTY(PersistentData | Replicated)
    MString EquippedItem = "starter_sword";

    void SetState(uint32 InGold, const MString& InEquippedItem);

    void LoadState(uint32 InGold, const MString& InEquippedItem);

    TResult<FPlayerChangeGoldResponse, FAppError> ApplyGoldDelta(int32 DeltaGold);

    MFUNCTION(ServerCall)
    MFuture<TResult<FPlayerQueryInventoryResponse, FAppError>> PlayerQueryInventory(
        const FPlayerQueryInventoryRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FPlayerChangeGoldResponse, FAppError>> PlayerChangeGold(
        const FPlayerChangeGoldRequest& Request);

    MFUNCTION(ServerCall)
    MFuture<TResult<FPlayerEquipItemResponse, FAppError>> PlayerEquipItem(
        const FPlayerEquipItemRequest& Request);
};
