#include "Tools/MObjectEditorService/Models/MonsterConfigEditorModel.h"

#include "Common/Runtime/Object/Object.h"
#include "Servers/Scene/Combat/MonsterConfig.h"

bool MMonsterConfigModelConverter::BuildRuntimeObject(
    const FEditorAssetIdentity& AssetId,
    const FMonsterConfigEditorModel& InModel,
    MMonsterConfig*& OutConfig,
    MString& OutError)
{
    OutConfig = NewMObject<MMonsterConfig>(nullptr, AssetId.AssetName);
    if (!OutConfig)
    {
        OutError = "monster_config_create_failed";
        return false;
    }

    OutConfig->SetMonsterTemplateId(InModel.MonsterTemplateId);
    OutConfig->SetDebugName(InModel.DebugName);
    OutConfig->SetSpawnParams(InModel.SpawnParams);
    OutConfig->SetSkillIds(InModel.SkillIds);
    return true;
}

bool MMonsterConfigModelConverter::BuildEditorModel(
    const MMonsterConfig& InConfig,
    FMonsterConfigEditorModel& OutModel,
    MString& OutError)
{
    (void)OutError;
    OutModel.MonsterTemplateId = InConfig.GetMonsterTemplateId();
    OutModel.DebugName = InConfig.GetDebugName();
    OutModel.SpawnParams = InConfig.GetSpawnParams();
    OutModel.SkillIds = InConfig.GetSkillIds();
    return true;
}
