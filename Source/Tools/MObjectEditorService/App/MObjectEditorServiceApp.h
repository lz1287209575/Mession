#pragma once

#include "Tools/MObjectEditorService/Documents/MonsterConfigDocument.h"
#include "Tools/MObjectEditorService/Export/AssetExportService.h"

struct FMonsterConfigSaveRequest
{
    FEditorAssetIdentity Identity;
    FMonsterConfigEditorModel Model;
    MString PreviousSourcePath;
};

struct FMonsterConfigSaveResult
{
    MString PreviousSourcePath;
    MString SourcePath;
    bool bOk = false;
    MString Error;
};

class MObjectEditorServiceApp
{
public:
    bool Initialize(MString& OutError);

    TVector<FEditorAssetIdentity> ListMonsterConfigs(MString& OutError) const;
    bool CreateNewMonsterConfig(const FEditorAssetIdentity& AssetId, MString& OutError);
    bool OpenMonsterConfig(const MString& FilePath, MString& OutError);
    bool SaveMonsterConfig(const FEditorAssetIdentity& AssetId, const FMonsterConfigEditorModel& Model, MString& OutError);
    bool SaveMonsterConfig(
        const FEditorAssetIdentity& AssetId,
        const FMonsterConfigEditorModel& Model,
        const MString& PreviousSourcePath,
        MString& OutError);
    TVector<FMonsterConfigSaveResult> SaveMonsterConfigsBatch(const TVector<FMonsterConfigSaveRequest>& Requests, bool& bOutHadFailures, MString& OutError);
    bool DeleteMonsterConfig(const FEditorAssetIdentity& AssetId, MString& OutError);
    bool SaveCurrentDocument(MString& OutError);

    TVector<FValidationIssue> ValidateMonsterConfig(const FMonsterConfigEditorModel& Model) const;
    TVector<FValidationIssue> ValidateCurrentDocument() const;
    FAssetExportResult ExportMonsterConfig(const FEditorAssetIdentity& AssetId, const FMonsterConfigEditorModel& Model, const FAssetExportOptions& Options) const;
    FAssetExportResult ExportCurrentDocument(const FAssetExportOptions& Options) const;

    const MMonsterConfigDocument* GetCurrentDocument() const { return CurrentMonsterConfigDocument.get(); }
    MMonsterConfigDocument* GetCurrentDocument() { return CurrentMonsterConfigDocument.get(); }

private:
    TUniquePtr<MMonsterConfigDocument> CurrentMonsterConfigDocument;
};
