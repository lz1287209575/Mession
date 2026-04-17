#pragma once

#include "Protocol/Messages/Combat/CombatSceneMessages.h"
#include "Tools/MObjectEditorService/Core/EditorTypes.h"

class MMonsterConfig;

struct FMonsterConfigEditorModel
{
    uint32 MonsterTemplateId = 0;
    MString DebugName;
    FSceneCombatMonsterSpawnParams SpawnParams;
    TVector<uint32> SkillIds;
};

class MMonsterConfigModelConverter
{
public:
    static bool BuildRuntimeObject(
        const FEditorAssetIdentity& AssetId,
        const FMonsterConfigEditorModel& InModel,
        MMonsterConfig*& OutConfig,
        MString& OutError);

    static bool BuildEditorModel(
        const MMonsterConfig& InConfig,
        FMonsterConfigEditorModel& OutModel,
        MString& OutError);
};
