#include "Gameplay/MovementMember.h"

#include "Gameplay/PlayerAvatar.h"

void MMovementMember::MoveTo(const SVector& NewLocation)
{
    if (MPlayerAvatar* Avatar = GetOwnerAvatar())
    {
        Avatar->SetLocation(NewLocation);
    }
}
