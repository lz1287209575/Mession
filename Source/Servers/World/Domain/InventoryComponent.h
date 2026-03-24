#pragma once

#include "Common/Runtime/Reflect/Reflection.h"

MCLASS(Type=Object)
class MInventoryComponent : public MObject
{
public:
    MGENERATED_BODY(MInventoryComponent, MObject, 0)
public:
    MPROPERTY(PersistentData | Replicated)
    uint32 Gold = 0;

    MPROPERTY(PersistentData | Replicated)
    MString EquippedItem = "starter_sword";

    void SetInventoryState(uint32 InGold, const MString& InEquippedItem);

    void LoadInventoryState(uint32 InGold, const MString& InEquippedItem);
};
