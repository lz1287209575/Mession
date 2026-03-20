#include "Servers/World/Avatar/MovementMember.h"

#include "Servers/World/Avatar/PlayerAvatar.h"

void MMovementMember::MoveTo(const SVector& NewLocation)
{
    if (MPlayerAvatar* Avatar = GetOwnerAvatar())
    {
        Avatar->SetLocation(NewLocation);
    }
}
