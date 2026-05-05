#include "Servers/World/Player/PlayerProgression.h"

#include "Servers/World/Player/Player.h"
#include "Servers/World/Player/PlayerPawn.h"
#include "Servers/World/Player/PlayerProfile.h"

#include <limits>

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

TResult<FPlayerGrantExperienceResponse, FAppError> MPlayerProgression::ApplyExperienceDelta(uint32 ExperienceDelta)
{
    if (ExperienceDelta == 0)
    {
        return TResult<FPlayerGrantExperienceResponse, FAppError>::Ok(FPlayerGrantExperienceResponse{});
    }

    const uint64 TotalExperience = static_cast<uint64>(Experience) + static_cast<uint64>(ExperienceDelta);
    if (TotalExperience > static_cast<uint64>((std::numeric_limits<uint32>::max)()))
    {
        return MakeErrorResult<FPlayerGrantExperienceResponse>(FAppError::Make(
            "player_experience_overflow",
            "PlayerGrantExperience"));
    }

    Experience = static_cast<uint32>(TotalExperience);

    // Current progression rule: every 100 experience grants one level and experience carries as total accumulated value.
    const uint32 NextLevel = 1 + (Experience / 100);
    if (NextLevel != Level)
    {
        Level = NextLevel;
        MarkPropertyDirty("Level");
    }

    MarkPropertyDirty("Experience");

    FPlayerGrantExperienceResponse Response;
    if (const MPlayerProfile* Profile = dynamic_cast<const MPlayerProfile*>(GetOuter()))
    {
        Response.PlayerId = Profile->PlayerId;
    }
    Response.Level = Level;
    Response.Experience = Experience;
    return TResult<FPlayerGrantExperienceResponse, FAppError>::Ok(std::move(Response));
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

    if (const MPlayerProfile* Profile = dynamic_cast<const MPlayerProfile*>(GetOuter()))
    {
        if (const MPlayer* Player = dynamic_cast<const MPlayer*>(Profile->GetOuter()))
        {
            Response.Health = Player->ResolveCurrentHealth();
        }
    }

    return MServerCallAsyncSupport::MakeSuccessFuture(std::move(Response));
}

MFuture<TResult<FPlayerGrantExperienceResponse, FAppError>> MPlayerProgression::PlayerGrantExperience(
    const FPlayerGrantExperienceRequest& Request)
{
    if (Request.ExperienceDelta == 0)
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerGrantExperienceResponse>(
            "experience_delta_required",
            "PlayerGrantExperience");
    }

    return MServerCallAsyncSupport::MakeResultFuture(ApplyExperienceDelta(Request.ExperienceDelta));
}

MFuture<TResult<FPlayerModifyHealthResponse, FAppError>> MPlayerProgression::PlayerModifyHealth(
    const FPlayerModifyHealthRequest& Request)
{
    const int64 NextHealth = static_cast<int64>(Health) + static_cast<int64>(Request.HealthDelta);
    uint32 ResolvedHealth = Health;
    if (NextHealth < 0)
    {
        ResolvedHealth = 0;
    }
    else if (NextHealth > static_cast<int64>((std::numeric_limits<uint32>::max)()))
    {
        return MServerCallAsyncSupport::MakeErrorFuture<FPlayerModifyHealthResponse>(
            "player_health_overflow",
            "PlayerModifyHealth");
    }
    else
    {
        ResolvedHealth = static_cast<uint32>(NextHealth);
    }

    if (MPlayerProfile* Profile = dynamic_cast<MPlayerProfile*>(GetOuter()))
    {
        if (MPlayer* Player = dynamic_cast<MPlayer*>(Profile->GetOuter()))
        {
            Player->ApplyResolvedHealth(ResolvedHealth);
        }
        else
        {
            SetHealth(ResolvedHealth);
        }
    }
    else
    {
        SetHealth(ResolvedHealth);
    }

    FPlayerModifyHealthResponse Response;
    if (const MPlayerProfile* Profile = dynamic_cast<const MPlayerProfile*>(GetOuter()))
    {
        Response.PlayerId = Profile->PlayerId;
    }
    Response.Health = Health;
    return MServerCallAsyncSupport::MakeSuccessFuture(std::move(Response));
}
