#pragma once

#include "Tools/MObjectEditorService/Core/EditorTypes.h"
#include "Tools/MObjectEditorService/Models/MonsterConfigEditorModel.h"

struct FAssetExportOptions
{
    bool bExportJson = true;
    bool bExportMob = true;
    bool bExportRoundTripJson = false;
    bool bPublishMob = false;
};

struct FAssetExportResult
{
    bool bSuccess = false;
    MString JsonPath;
    MString MobPath;
    MString RoundTripPath;
    MString PublishPath;
    MString Error;
    TVector<FValidationIssue> Issues;
};

class MAssetExportService
{
public:
    static FAssetExportResult ExportMonsterConfig(
        const FEditorAssetIdentity& AssetId,
        const FMonsterConfigEditorModel& Model,
        const FAssetExportOptions& Options);
};
