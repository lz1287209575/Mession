#include "Servers/World/Players/PlayerProgression.h"

void MPlayerProgression::SetState(uint32 InLevel, uint32 InExperience, uint32 InHealth)
{
    Level = InLevel;
    Experience = InExperience;
    Health = InHealth;
    MarkPropertyDirty("Level");
    MarkPropertyDirty("Experience");
    MarkPropertyDirty("Health");
}

void MPlayerProgression::LoadState(uint32 InLevel, uint32 InExperience, uint32 InHealth)
{
    Level = InLevel;
    Experience = InExperience;
    Health = InHealth;
}
