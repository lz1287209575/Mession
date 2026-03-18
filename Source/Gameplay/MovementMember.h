#pragma once

#include "Gameplay/AvatarMember.h"
#include "Core/Net/NetCore.h"

MCLASS()
class MMovementMember : public MAvatarMember
{
public:
    MGENERATED_BODY(MMovementMember, MAvatarMember, 0)

public:
    MFUNCTION()
    void MoveTo(const SVector& NewLocation);
};
