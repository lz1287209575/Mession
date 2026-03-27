#pragma once

#include "Common/Runtime/Object/Object.h"
#include "Common/Runtime/Reflect/Reflection.h"
#include "Protocol/Messages/World/WorldPlayerMessages.h"
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
};
