#pragma once

#include "Common/Skill/SkillNodeRegistry.h"
#include "Common/Runtime/MLib.h"

enum class ESkillTargetType : uint8
{
    EnemySingle = 0,
    Self = 1,
};

struct FSkillStep
{
    uint32 StepIndex = 0;
    ESkillServerOp Op = ESkillServerOp::Start;
    TVector<uint32> NextStepIndices;
    ESkillTargetType TargetType = ESkillTargetType::EnemySingle;
    float RequiredRange = 0.0f;
    float BaseDamage = 0.0f;
    float AttackPowerScale = 0.0f;
    int32 IntParam0 = 0;
    MString NameParam;
    MString StringParam;

    uint32 ComputeAppliedDamage(uint32 AttackPower, uint32 DefensePower) const
    {
        float RawDamage = BaseDamage + (static_cast<float>(AttackPower) * AttackPowerScale);
        if (RawDamage < 1.0f)
        {
            RawDamage = 1.0f;
        }

        const float MitigatedDamage = RawDamage - static_cast<float>(DefensePower);
        if (MitigatedDamage <= 1.0f)
        {
            return 1;
        }

        return static_cast<uint32>(MitigatedDamage);
    }
};

struct FSkillSpec
{
    uint32 SchemaVersion = 1;
    uint32 SkillId = 0;
    MString SkillName;
    ESkillTargetType TargetType = ESkillTargetType::EnemySingle;
    float CastRange = 0.0f;
    uint32 CooldownMs = 0;
    float CastTimeSeconds = 0.0f;
    float BaseDamage = 0.0f;
    float AttackPowerScale = 1.0f;
    bool bCanTargetSelf = false;
    TVector<FSkillStep> Steps;

    uint32 ComputeAppliedDamage(uint32 AttackPower, uint32 DefensePower) const
    {
        float RawDamage = BaseDamage + (static_cast<float>(AttackPower) * AttackPowerScale);
        if (RawDamage < 1.0f)
        {
            RawDamage = 1.0f;
        }

        const float MitigatedDamage = RawDamage - static_cast<float>(DefensePower);
        if (MitigatedDamage <= 1.0f)
        {
            return 1;
        }

        return static_cast<uint32>(MitigatedDamage);
    }
};
