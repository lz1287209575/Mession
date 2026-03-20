#include "Servers/World/Avatar/InteractionMember.h"

#include "Common/Runtime/Log/Logger.h"
#include "Servers/World/Avatar/PlayerAvatar.h"

bool MInteractionMember::Interact(uint64 TargetActorId)
{
    MPlayerAvatar* Avatar = GetOwnerAvatar();
    if (!Avatar || TargetActorId == 0)
    {
        return false;
    }

    LOG_INFO("Avatar %llu interact request target=%llu",
             static_cast<unsigned long long>(Avatar->GetObjectId()),
             static_cast<unsigned long long>(TargetActorId));
    return true;
}
