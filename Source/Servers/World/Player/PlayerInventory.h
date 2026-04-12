#pragma once

#include "Common/Runtime/Object/Object.h"
#include "Common/Runtime/Reflect/Reflection.h"
#include "Protocol/Messages/World/PlayerModifyMessages.h"
#include "Protocol/Messages/World/PlayerQueryMessages.h"
#include "Servers/App/ServerCallAsyncSupport.h"

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
