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

    // Persistence bridge for runtime health until Pawn persistence has a dedicated migration path.
    MPROPERTY(PersistentData | Replicated)
    uint32 Health = 100;

    void SetState(uint32 InLevel, uint32 InExperience, uint32 InHealth);

    void LoadState(uint32 InLevel, uint32 InExperience, uint32 InHealth);
};
