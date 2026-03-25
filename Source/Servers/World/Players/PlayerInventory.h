#pragma once

#include "Common/Runtime/Reflect/Reflection.h"

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
};
