#include "Servers/World/Players/PlayerProgression.h"

#include "Servers/World/Players/PlayerProfile.h"

void MPlayerProgression::SetState(uint32 InLevel, uint32 InExperience, uint32 InHealth)
{
    Level = InLevel;
    Experience = InExperience;
    Health = InHealth;
    MarkPropertyDirty("Level");
    MarkPropertyDirty("Experience");
    MarkPropertyDirty("Health");
}

void MPlayerProgression::SetHealth(uint32 InHealth)
{
    Health = InHealth;
    MarkPropertyDirty("Health");
}

void MPlayerProgression::LoadState(uint32 InLevel, uint32 InExperience, uint32 InHealth)
{
    Level = InLevel;
    Experience = InExperience;
    Health = InHealth;
}

MFuture<TResult<FPlayerQueryProgressionResponse, FAppError>> MPlayerProgression::PlayerQueryProgression(
    const FPlayerQueryProgressionRequest& /*Request*/)
{
    FPlayerQueryProgressionResponse Response;
    if (const MPlayerProfile* Profile = dynamic_cast<const MPlayerProfile*>(GetOuter()))
    {
        Response.PlayerId = Profile->PlayerId;
    }

    Response.Level = Level;
    Response.Experience = Experience;
    Response.Health = Health;
    return MServerCallAsyncSupport::MakeSuccessFuture(std::move(Response));
}
