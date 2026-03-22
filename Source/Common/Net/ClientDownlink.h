#pragma once

#include "Common/Runtime/Object/Object.h"
#include "Protocol/Messages/InventoryMessages.h"

MCLASS()
class MClientDownlink : public MObject
{
public:
    MGENERATED_BODY(MClientDownlink, MObject, 0)

    MFUNCTION()
    void Client_OnLoginResponse(uint32, uint64) {}

    MFUNCTION()
    void Client_OnActorCreate(uint64, const TByteArray&) {}

    MFUNCTION()
    void Client_OnActorUpdate(uint64, const TByteArray&) {}

    MFUNCTION()
    void Client_OnActorDestroy(uint64) {}

    MFUNCTION()
    void Client_OnInventoryPull(
        uint64,
        int32,
        uint32,
        const TVector<SInventoryItemPayload>&) {}
};
