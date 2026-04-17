#include "Servers/Scene/Combat/MonsterCombatProfile.h"

void MMonsterCombatProfile::Initialize(
    uint32 InBaseAttack,
    uint32 InBaseDefense,
    uint32 InMaxHealth,
    uint32 InPrimarySkillId,
    uint32 InExperienceReward,
    uint32 InGoldReward)
{
    BaseAttack = InBaseAttack;
    BaseDefense = InBaseDefense;
    MaxHealth = InMaxHealth != 0 ? InMaxHealth : 1;
    PrimarySkillId = InPrimarySkillId != 0 ? InPrimarySkillId : 1001;
    ExperienceReward = InExperienceReward;
    GoldReward = InGoldReward;
}
