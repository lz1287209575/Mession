#include "Servers/Scene/Combat/SkillCatalog.h"

#include "Servers/Scene/Combat/UAssetSkillLoader.h"

#include <filesystem>

void MSkillCatalog::LoadBuiltInDefaults()
{
    FSkillSpec BasicAttack;
    BasicAttack.SkillId = 1001;
    BasicAttack.SkillName = "BasicAttack";
    BasicAttack.TargetType = ESkillTargetType::EnemySingle;
    BasicAttack.CastRange = 500.0f;
    BasicAttack.CooldownMs = 0;
    BasicAttack.BaseDamage = 0.0f;
    BasicAttack.AttackPowerScale = 1.0f;
    BasicAttack.bCanTargetSelf = false;
    BasicAttack.Steps = {
        FSkillStep {.StepIndex = 0, .Op = ESkillServerOp::Start},
        FSkillStep {.StepIndex = 1, .Op = ESkillServerOp::SelectTarget, .TargetType = ESkillTargetType::EnemySingle},
        FSkillStep {.StepIndex = 2, .Op = ESkillServerOp::ApplyDamage, .BaseDamage = 0.0f, .AttackPowerScale = 1.0f},
        FSkillStep {.StepIndex = 3, .Op = ESkillServerOp::End},
    };
    (void)RegisterSkill(BasicAttack);
}

bool MSkillCatalog::LoadFromDirectory(const MString& DirectoryPath, TVector<MString>* OutWarnings)
{
    namespace fs = std::filesystem;

    if (DirectoryPath.empty())
    {
        return true;
    }

    std::error_code Error;
    if (!fs::exists(DirectoryPath, Error))
    {
        if (OutWarnings)
        {
            OutWarnings->push_back("skill_asset_directory_missing");
        }
        return true;
    }

    if (!fs::is_directory(DirectoryPath, Error))
    {
        if (OutWarnings)
        {
            OutWarnings->push_back("skill_asset_directory_invalid");
        }
        return false;
    }

    for (const fs::directory_entry& Entry : fs::directory_iterator(DirectoryPath, Error))
    {
        if (Error)
        {
            if (OutWarnings)
            {
                OutWarnings->push_back("skill_asset_directory_iter_failed");
            }
            return false;
        }

        if (!Entry.is_regular_file())
        {
            continue;
        }

        const fs::path AssetPath = Entry.path();
        if (AssetPath.extension() != ".uasset")
        {
            continue;
        }

        FSkillSpec SkillSpec;
        MString LoadError;
        if (!FUAssetSkillLoader::LoadSkillSpecFromFile(AssetPath.string(), SkillSpec, LoadError))
        {
            if (OutWarnings)
            {
                OutWarnings->push_back(AssetPath.filename().string() + ":" + LoadError);
            }
            continue;
        }

        MString RegisterError;
        if (!RegisterSkill(SkillSpec, &RegisterError) && OutWarnings)
        {
            OutWarnings->push_back(AssetPath.filename().string() + ":" + RegisterError);
        }
    }

    return true;
}

bool MSkillCatalog::RegisterSkill(const FSkillSpec& SkillSpec, MString* OutError)
{
    if (SkillSpec.SkillId == 0)
    {
        if (OutError)
        {
            *OutError = "skill_id_required";
        }
        return false;
    }

    SkillsById[SkillSpec.SkillId] = SkillSpec;
    return true;
}

const FSkillSpec* MSkillCatalog::FindSkill(uint32 SkillId) const
{
    const auto It = SkillsById.find(SkillId);
    return It != SkillsById.end() ? &It->second : nullptr;
}
