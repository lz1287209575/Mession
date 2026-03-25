#include "Servers/World/Domain/AttributeComponent.h"

void MAttributeComponent::SetProgression(uint32 InLevel, uint32 InExperience, uint32 InHealth)
{
    Level = InLevel;
    Experience = InExperience;
    Health = InHealth;
    MarkPropertyDirty("Level");
    MarkPropertyDirty("Experience");
    MarkPropertyDirty("Health");
}

void MAttributeComponent::LoadProgression(uint32 InLevel, uint32 InExperience, uint32 InHealth)
{
    Level = InLevel;
    Experience = InExperience;
    Health = InHealth;
}
