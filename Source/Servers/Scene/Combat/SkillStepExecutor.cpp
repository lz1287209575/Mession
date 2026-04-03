#include "Servers/Scene/Combat/SkillStepExecutor.h"

const MSkillStepExecutor::FStepHandler MSkillStepExecutor::StepHandlers[static_cast<size_t>(ESkillServerOp::Count)] = {
#define MESSION_SKILL_NODE(OpName, StableNodeIdValue, RegistryNameLiteral, DisplayNameLiteral, CategoryValue, EditorNodeTokenLiteral, MinOutgoingEdgesValue, MaxOutgoingEdgesValue, UsesSkillTargetTypeValue, FloatParam0KeyLiteral, FloatParam0DisplayNameLiteral, FloatParam0RequiredValue, Float0Semantic, FloatParam1KeyLiteral, FloatParam1DisplayNameLiteral, FloatParam1RequiredValue, Float1Semantic, IntParam0KeyLiteral, IntParam0DisplayNameLiteral, IntParam0RequiredValue, NameParamKeyLiteral, NameParamDisplayNameLiteral, NameParamRequiredValue, StringParamKeyLiteral, StringParamDisplayNameLiteral, StringParamRequiredValue) \
    &MSkillStepExecutor::Execute##OpName,
#include "Common/Skill/SkillNodeRegistry.def"
#undef MESSION_SKILL_NODE
};

bool MSkillStepExecutor::ExecuteSkill(
    const FSkillSpec& Spec,
    FSkillExecutionContext& Context,
    MString& OutError) const
{
    if (!Context.CasterSnapshot)
    {
        OutError = "skill_executor_caster_missing";
        return false;
    }

    if (Spec.Steps.empty())
    {
        OutError = "skill_steps_missing";
        return false;
    }

    for (const FSkillStep& Step : Spec.Steps)
    {
        const size_t OpIndex = static_cast<size_t>(Step.Op);
        if (OpIndex >= static_cast<size_t>(ESkillServerOp::Count))
        {
            OutError = "skill_step_op_invalid";
            return false;
        }

        const FStepHandler Handler = StepHandlers[OpIndex];
        if (!Handler)
        {
            OutError = "skill_step_handler_missing";
            return false;
        }

        if (!(this->*Handler)(Spec, Step, Context, OutError))
        {
            return false;
        }

        if (Context.bFailed || Context.bFinished)
        {
            break;
        }
    }

    if (Context.bFailed)
    {
        OutError = Context.FailReason.empty() ? "skill_execution_failed" : Context.FailReason;
        return false;
    }

    return true;
}

bool MSkillStepExecutor::ExecuteStart(
    const FSkillSpec& /*Spec*/,
    const FSkillStep& /*Step*/,
    FSkillExecutionContext& /*Context*/,
    MString& /*OutError*/) const
{
    return true;
}

bool MSkillStepExecutor::ExecuteSelectTarget(
    const FSkillSpec& Spec,
    const FSkillStep& Step,
    FSkillExecutionContext& Context,
    MString& OutError) const
{
    Context.SelectedTargets.clear();

    const ESkillTargetType EffectiveTargetType =
        Step.TargetType == ESkillTargetType::EnemySingle ? Spec.TargetType : Step.TargetType;

    if (EffectiveTargetType == ESkillTargetType::Self)
    {
        if (!Context.CasterSnapshot)
        {
            OutError = "skill_executor_caster_missing";
            return false;
        }

        Context.SelectedTargets.push_back(const_cast<FSceneCombatAvatarSnapshot*>(Context.CasterSnapshot));
        return true;
    }

    if (!Context.PrimaryTargetSnapshot)
    {
        OutError = "skill_executor_primary_target_missing";
        return false;
    }

    Context.SelectedTargets.push_back(Context.PrimaryTargetSnapshot);
    return true;
}

bool MSkillStepExecutor::ExecuteCheckRange(
    const FSkillSpec& /*Spec*/,
    const FSkillStep& /*Step*/,
    FSkillExecutionContext& /*Context*/,
    MString& /*OutError*/) const
{
    // Range validation needs authoritative scene position data. Leave this as a
    // no-op in the first executor slice so compiled steps can already model the
    // intended order without blocking current combat flow.
    return true;
}

bool MSkillStepExecutor::ExecuteApplyDamage(
    const FSkillSpec& Spec,
    const FSkillStep& Step,
    FSkillExecutionContext& Context,
    MString& OutError) const
{
    if (!Context.CasterSnapshot)
    {
        OutError = "skill_executor_caster_missing";
        return false;
    }

    if (Context.SelectedTargets.empty() || !Context.SelectedTargets.front())
    {
        OutError = "skill_executor_target_missing";
        return false;
    }

    FSceneCombatAvatarSnapshot* TargetSnapshot = Context.SelectedTargets.front();
    uint32 AppliedDamage = 0;
    if (Step.BaseDamage > 0.0f || Step.AttackPowerScale > 0.0f)
    {
        AppliedDamage = Step.ComputeAppliedDamage(
            Context.CasterSnapshot->AttackPower,
            TargetSnapshot->DefensePower);
    }
    else
    {
        AppliedDamage = Spec.ComputeAppliedDamage(
            Context.CasterSnapshot->AttackPower,
            TargetSnapshot->DefensePower);
    }

    if (TargetSnapshot->CurrentHealth <= AppliedDamage)
    {
        TargetSnapshot->CurrentHealth = 0;
    }
    else
    {
        TargetSnapshot->CurrentHealth -= AppliedDamage;
    }

    Context.AppliedDamage += AppliedDamage;
    return true;
}

bool MSkillStepExecutor::ExecuteEnd(
    const FSkillSpec& /*Spec*/,
    const FSkillStep& /*Step*/,
    FSkillExecutionContext& Context,
    MString& /*OutError*/) const
{
    Context.bFinished = true;
    return true;
}
