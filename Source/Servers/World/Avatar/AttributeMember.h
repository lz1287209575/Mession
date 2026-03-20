#pragma once

#include "Servers/World/Avatar/AvatarMember.h"

MCLASS()
class MAttributeMember : public MAvatarMember
{
public:
    MGENERATED_BODY(MAttributeMember, MAvatarMember, 0)

public:
    MPROPERTY(Edit | SaveGame)
    float Health = 100.0f;

    MPROPERTY(Edit | SaveGame)
    float MaxHealth = 100.0f;

    MPROPERTY(Edit | SaveGame)
    float MoveSpeed = 300.0f;

    MPROPERTY(Edit | SaveGame)
    bool bAlive = true;

    MFUNCTION()
    void ApplyDamage(float Damage);

    MFUNCTION()
    void Heal(float Amount);
};
