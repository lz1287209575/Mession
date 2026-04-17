#include "Servers/Scene/Combat/MonsterFactory.h"

#include "Common/Runtime/Object/Object.h"
#include "Common/Runtime/StringUtils.h"
#include "Servers/Scene/Combat/Monster.h"
#include "Servers/Scene/Combat/MonsterConfig.h"

bool MMonsterFactory::RegisterConfig(MMonsterConfig* Config, MString& OutError)
{
    if (!Config)
    {
        OutError = "monster_config_required";
        return false;
    }

    const uint32 MonsterTemplateId = Config->GetMonsterTemplateId();
    if (MonsterTemplateId == 0)
    {
        OutError = "monster_config_template_id_required";
        return false;
    }

    const auto ExistingIt = ConfigsByMonsterTemplateId.find(MonsterTemplateId);
    if (ExistingIt != ConfigsByMonsterTemplateId.end() && ExistingIt->second != Config)
    {
        OutError = "monster_config_duplicate_template_id";
        return false;
    }

    ConfigsByMonsterTemplateId[MonsterTemplateId] = Config;
    return true;
}

const MMonsterConfig* MMonsterFactory::FindConfig(uint32 MonsterTemplateId) const
{
    const auto It = ConfigsByMonsterTemplateId.find(MonsterTemplateId);
    return It != ConfigsByMonsterTemplateId.end() ? It->second : nullptr;
}

void MMonsterFactory::ApplyConfigOverrides(
    const FSceneCombatMonsterSpawnParams& Overrides,
    FSceneCombatMonsterSpawnParams& InOutParams) const
{
    if (Overrides.SceneId != 0)
    {
        InOutParams.SceneId = Overrides.SceneId;
    }
    if (Overrides.MonsterTemplateId != 0)
    {
        InOutParams.MonsterTemplateId = Overrides.MonsterTemplateId;
    }
    if (!Overrides.DebugName.empty())
    {
        InOutParams.DebugName = Overrides.DebugName;
    }
    if (Overrides.CurrentHealth != 0)
    {
        InOutParams.CurrentHealth = Overrides.CurrentHealth;
    }
    if (Overrides.MaxHealth != 0)
    {
        InOutParams.MaxHealth = Overrides.MaxHealth;
    }
    if (Overrides.AttackPower != 0)
    {
        InOutParams.AttackPower = Overrides.AttackPower;
    }
    if (Overrides.DefensePower != 0)
    {
        InOutParams.DefensePower = Overrides.DefensePower;
    }
    if (Overrides.PrimarySkillId != 0)
    {
        InOutParams.PrimarySkillId = Overrides.PrimarySkillId;
    }
    if (Overrides.ExperienceReward != 0)
    {
        InOutParams.ExperienceReward = Overrides.ExperienceReward;
    }
    if (Overrides.GoldReward != 0)
    {
        InOutParams.GoldReward = Overrides.GoldReward;
    }
}

bool MMonsterFactory::ResolveSpawnParams(
    const FSceneCombatMonsterSpawnParams& Params,
    FSceneCombatMonsterSpawnParams& OutResolvedParams) const
{
    if (const MMonsterConfig* Config = FindConfig(Params.MonsterTemplateId))
    {
        OutResolvedParams = Config->BuildSpawnParams();
        ApplyConfigOverrides(Params, OutResolvedParams);
        return true;
    }

    OutResolvedParams = Params;
    return true;
}

MMonster* MMonsterFactory::CreateMonster(
    MObject* Owner,
    uint64 CombatEntityId,
    const FSceneCombatMonsterSpawnParams& Params,
    MString& OutError) const
{
    if (!Owner)
    {
        OutError = "monster_owner_required";
        return nullptr;
    }

    if (CombatEntityId == 0)
    {
        OutError = "combat_entity_id_required";
        return nullptr;
    }

    FSceneCombatMonsterSpawnParams ResolvedParams;
    if (!ResolveSpawnParams(Params, ResolvedParams))
    {
        OutError = "monster_spawn_params_resolve_failed";
        return nullptr;
    }

    MString Name = "Monster_" + MStringUtil::ToString(CombatEntityId);
    if (!ResolvedParams.DebugName.empty())
    {
        Name += "_" + ResolvedParams.DebugName;
    }

    MMonster* Monster = NewMObject<MMonster>(Owner, Name);
    Monster->InitializeForSpawn(CombatEntityId, ResolvedParams);
    return Monster;
}

MMonster* MMonsterFactory::CreateMonster(
    MObject* Owner,
    uint64 CombatEntityId,
    const MMonsterConfig& Config,
    MString& OutError) const
{
    return CreateMonster(Owner, CombatEntityId, Config.BuildSpawnParams(), OutError);
}

void MMonsterFactory::VisitReferencedObjects(const TFunction<void(MObject*)>& Visitor) const
{
    MObject::VisitReferencedObjects(Visitor);
    if (!Visitor)
    {
        return;
    }

    for (const auto& [MonsterTemplateId, Config] : ConfigsByMonsterTemplateId)
    {
        (void)MonsterTemplateId;
        Visitor(Config);
    }
}
