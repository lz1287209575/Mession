#include "Gameplay/InteractionMember.h"

#include "Common/Logger.h"
#include "Gameplay/PlayerAvatar.h"

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
