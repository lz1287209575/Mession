#pragma once

#include "Common/Runtime/Object/Object.h"
#include "Common/Runtime/Reflect/Reflection.h"

MCLASS(Type=Object)
class MMonsterCombatProfile : public MObject
{
public:
    MGENERATED_BODY(MMonsterCombatProfile, MObject, 0)
public:
    MPROPERTY()
    uint32 BaseAttack = 0;

    MPROPERTY()
    uint32 BaseDefense = 0;

    MPROPERTY()
    uint32 MaxHealth = 1;

    MPROPERTY()
    uint32 PrimarySkillId = 1001;

    MPROPERTY()
    uint32 ExperienceReward = 0;

    MPROPERTY()
    uint32 GoldReward = 0;

    void Initialize(
        uint32 InBaseAttack,
        uint32 InBaseDefense,
        uint32 InMaxHealth,
        uint32 InPrimarySkillId,
        uint32 InExperienceReward,
        uint32 InGoldReward);
};
