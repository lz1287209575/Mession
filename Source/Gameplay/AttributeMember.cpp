#include "Gameplay/AttributeMember.h"
#include "MAttributeMember.mgenerated.h"

void MAttributeMember::ApplyDamage(float Damage)
{
    if (Damage <= 0.0f)
    {
        return;
    }

    Health -= Damage;
    SetPropertyDirty(Prop_MAttributeMember_Health());
    if (Health <= 0.0f)
    {
        Health = 0.0f;
        bAlive = false;
        SetPropertyDirty(Prop_MAttributeMember_Health());
        SetPropertyDirty(Prop_MAttributeMember_bAlive());
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
    SetPropertyDirty(Prop_MAttributeMember_Health());
    if (Health > 0.0f)
    {
        bAlive = true;
        SetPropertyDirty(Prop_MAttributeMember_bAlive());
    }
}
