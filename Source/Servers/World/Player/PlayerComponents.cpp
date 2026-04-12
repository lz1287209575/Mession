#include "Servers/World/Player/Player.h"

#include "Servers/World/Player/PlayerCombatProfile.h"
#include "Servers/World/Player/PlayerController.h"
#include "Servers/World/Player/PlayerPawn.h"
#include "Servers/World/Player/PlayerProfile.h"
#include "Servers/World/Player/PlayerSession.h"

MPlayer::MPlayer()
{
    Session = CreateDefaultSubObject<MPlayerSession>(this, "Session");
    Controller = CreateDefaultSubObject<MPlayerController>(this, "Controller");
    Pawn = CreateDefaultSubObject<MPlayerPawn>(this, "Pawn");
    Profile = CreateDefaultSubObject<MPlayerProfile>(this, "Profile");
    CombatProfile = CreateDefaultSubObject<MPlayerCombatProfile>(this, "CombatProfile");
}

MPlayerSession* MPlayer::GetSession() const
{
    return Session;
}

MPlayerController* MPlayer::GetController() const
{
    return Controller;
}

MPlayerPawn* MPlayer::GetPawn() const
{
    return Pawn;
}

MPlayerProfile* MPlayer::GetProfile() const
{
    return Profile;
}

MPlayerCombatProfile* MPlayer::GetCombatProfile() const
{
    return CombatProfile;
}

void MPlayer::VisitReferencedObjects(const TFunction<void(MObject*)>& Visitor) const
{
    MObject::VisitReferencedObjects(Visitor);
    if (Visitor)
    {
        Visitor(Session);
        Visitor(Controller);
        Visitor(Pawn);
        Visitor(Profile);
        Visitor(CombatProfile);
    }
}
