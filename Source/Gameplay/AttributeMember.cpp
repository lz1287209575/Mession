#include "Gameplay/AttributeMember.h"

void MAttributeMember::ApplyDamage(float Damage)
{
    if (Damage <= 0.0f)
    {
        return;
    }

    Health -= Damage;
    if (Health <= 0.0f)
    {
        Health = 0.0f;
        bAlive = false;
    }
}

void MAttributeMember::Heal(float Amount)
{
    if (Amount <= 0.0f)
    {
        return;
    }

    Health += Amount;
    if (Health > MaxHealth)
    {
        Health = MaxHealth;
    }
    if (Health > 0.0f)
    {
        bAlive = true;
    }
}
