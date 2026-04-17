#include "Servers/Scene/Combat/MonsterConfig.h"

uint32 MMonsterConfig::GetMonsterTemplateId() const
{
    return MonsterTemplateId;
}

const MString& MMonsterConfig::GetDebugName() const
{
    return DebugName;
}

const FSceneCombatMonsterSpawnParams& MMonsterConfig::GetSpawnParams() const
{
    return SpawnParams;
}

const TVector<uint32>& MMonsterConfig::GetSkillIds() const
{
    return SkillIds;
}

void MMonsterConfig::SetMonsterTemplateId(uint32 InMonsterTemplateId)
{
    MonsterTemplateId = InMonsterTemplateId;
}

void MMonsterConfig::SetDebugName(const MString& InDebugName)
{
    DebugName = InDebugName;
}

void MMonsterConfig::SetSpawnParams(const FSceneCombatMonsterSpawnParams& InSpawnParams)
{
    SpawnParams = InSpawnParams;
}

void MMonsterConfig::SetSkillIds(const TVector<uint32>& InSkillIds)
{
    SkillIds = InSkillIds;
}

FSceneCombatMonsterSpawnParams MMonsterConfig::BuildSpawnParams() const
{
    FSceneCombatMonsterSpawnParams Params = SpawnParams;
    Params.MonsterTemplateId = MonsterTemplateId;
    if (!DebugName.empty())
    {
        Params.DebugName = DebugName;
    }
    if (Params.PrimarySkillId == 0 && !SkillIds.empty())
    {
        Params.PrimarySkillId = SkillIds.front();
    }
    return Params;
}
