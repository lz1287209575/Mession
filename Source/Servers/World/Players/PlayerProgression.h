#pragma once

#include "Common/Runtime/Reflect/Reflection.h"

MCLASS(Type=Object)
class MPlayerProgression : public MObject
{
public:
    MGENERATED_BODY(MPlayerProgression, MObject, 0)
public:
    MPROPERTY(PersistentData | Replicated)
    uint32 Level = 1;

    MPROPERTY(PersistentData | Replicated)
    uint32 Experience = 0;

    // Health is temporarily kept with progression persistence until scene pawn state is introduced.
    MPROPERTY(PersistentData | Replicated)
    uint32 Health = 100;

    void SetState(uint32 InLevel, uint32 InExperience, uint32 InHealth);

    void LoadState(uint32 InLevel, uint32 InExperience, uint32 InHealth);
};
