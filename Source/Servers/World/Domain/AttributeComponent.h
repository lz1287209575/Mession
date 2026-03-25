#pragma once

#include "Common/Runtime/Reflect/Reflection.h"

MCLASS(Type=Object)
class MAttributeComponent : public MObject
{
public:
    MGENERATED_BODY(MAttributeComponent, MObject, 0)
public:
    MPROPERTY(PersistentData | Replicated)
    uint32 Level = 1;

    MPROPERTY(PersistentData | Replicated)
    uint32 Experience = 0;

    MPROPERTY(PersistentData | Replicated)
    uint32 Health = 100;

    void SetProgression(uint32 InLevel, uint32 InExperience, uint32 InHealth);

    void LoadProgression(uint32 InLevel, uint32 InExperience, uint32 InHealth);
};
