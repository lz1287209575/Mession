#include "Servers/World/Avatar/AvatarMember.h"
#include "MAvatarMember.mgenerated.h"

void MAvatarMember::SetOwnerPlayerId(uint64 InOwnerPlayerId)
{
    if (OwnerPlayerId == InOwnerPlayerId)
    {
        return;
    }

    OwnerPlayerId = InOwnerPlayerId;
    SetPropertyDirty(Prop_MAvatarMember_OwnerPlayerId());
}
