#pragma once

#include "Common/Runtime/MLib.h"
#include "Servers/Scene/Combat/SkillExecutionContext.h"
#include "Servers/Scene/Combat/SkillSpec.h"

class MSkillStepExecutor
{
public:
    bool ExecuteSkill(
        const FSkillSpec& Spec,
        FSkillExecutionContext& Context,
        MString& OutError) const;

private:
    using FStepHandler = bool (MSkillStepExecutor::*)(
        const FSkillSpec& Spec,
        const FSkillStep& Step,
        FSkillExecutionContext& Context,
        MString& OutError) const;

    static const FStepHandler StepHandlers[static_cast<size_t>(ESkillServerOp::Count)];

    bool ExecuteStart(
        const FSkillSpec& Spec,
        const FSkillStep& Step,
        FSkillExecutionContext& Context,
        MString& OutError) const;

    bool ExecuteSelectTarget(
        const FSkillSpec& Spec,
        const FSkillStep& Step,
        FSkillExecutionContext& Context,
        MString& OutError) const;

    bool ExecuteCheckRange(
        const FSkillSpec& Spec,
        const FSkillStep& Step,
        FSkillExecutionContext& Context,
        MString& OutError) const;

    bool ExecuteApplyDamage(
        const FSkillSpec& Spec,
        const FSkillStep& Step,
        FSkillExecutionContext& Context,
        MString& OutError) const;

    bool ExecuteEnd(
        const FSkillSpec& Spec,
        const FSkillStep& Step,
        FSkillExecutionContext& Context,
        MString& OutError) const;
};
