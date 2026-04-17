#include "Tools/MObjectEditorService/Validation/MonsterConfigValidator.h"

namespace
{
void AddIssue(
    TVector<FValidationIssue>& OutIssues,
    EValidationSeverity Severity,
    const MString& FieldPath,
    const MString& Code,
    const MString& Message)
{
    FValidationIssue Issue;
    Issue.Severity = Severity;
    Issue.FieldPath = FieldPath;
    Issue.Code = Code;
    Issue.Message = Message;
    OutIssues.push_back(std::move(Issue));
}
}

TVector<FValidationIssue> MMonsterConfigValidator::Validate(const FMonsterConfigEditorModel& Model)
{
    TVector<FValidationIssue> Issues;

    if (Model.MonsterTemplateId == 0)
    {
        AddIssue(Issues, EValidationSeverity::Error, "MonsterTemplateId", "monster_template_id_required", "MonsterTemplateId must not be 0");
    }
    if (Model.SpawnParams.SceneId == 0)
    {
        AddIssue(Issues, EValidationSeverity::Error, "SpawnParams.SceneId", "scene_id_required", "SceneId must not be 0");
    }
    if (Model.SpawnParams.MaxHealth == 0)
    {
        AddIssue(Issues, EValidationSeverity::Error, "SpawnParams.MaxHealth", "max_health_required", "MaxHealth must be greater than 0");
    }
    if (Model.SpawnParams.AttackPower == 0)
    {
        AddIssue(Issues, EValidationSeverity::Error, "SpawnParams.AttackPower", "attack_power_required", "AttackPower must be greater than 0");
    }
    if (Model.SpawnParams.DefensePower == 0)
    {
        AddIssue(Issues, EValidationSeverity::Error, "SpawnParams.DefensePower", "defense_power_required", "DefensePower must be greater than 0");
    }
    if (Model.SkillIds.empty())
    {
        AddIssue(Issues, EValidationSeverity::Warning, "SkillIds", "skill_ids_empty", "SkillIds is empty");
    }
    if (Model.SpawnParams.PrimarySkillId == 0 && Model.SkillIds.empty())
    {
        AddIssue(Issues, EValidationSeverity::Error, "SpawnParams.PrimarySkillId", "primary_skill_missing", "PrimarySkillId must be set or SkillIds must provide a fallback");
    }

    return Issues;
}
