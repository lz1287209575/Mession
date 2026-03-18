#pragma once

#include "Gameplay/AvatarMember.h"

MCLASS()
class MAttributeMember : public MAvatarMember
{
public:
    MGENERATED_BODY(MAttributeMember, MAvatarMember, 0)

public:
    MPROPERTY(Edit)
    float Health = 100.0f;

    MPROPERTY(Edit)
    float MaxHealth = 100.0f;

    MPROPERTY(Edit)
    float MoveSpeed = 300.0f;

    MPROPERTY(Edit)
    bool bAlive = true;

    MFUNCTION()
    void ApplyDamage(float Damage);

    MFUNCTION()
    void Heal(float Amount);
};
