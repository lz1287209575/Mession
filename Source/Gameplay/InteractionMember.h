#pragma once

#include "Gameplay/AvatarMember.h"

MCLASS()
class MInteractionMember : public MAvatarMember
{
public:
    MGENERATED_BODY(MInteractionMember, MAvatarMember, 0)

public:
    MFUNCTION()
    bool Interact(uint64 TargetActorId);
};
