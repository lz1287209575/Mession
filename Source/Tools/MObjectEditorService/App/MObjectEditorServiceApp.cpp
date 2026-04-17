#include "Tools/MObjectEditorService/App/MObjectEditorServiceApp.h"

#include "Tools/MObjectEditorService/Core/EditorPaths.h"
#include "Tools/MObjectEditorService/Validation/MonsterConfigValidator.h"

#include <algorithm>
#include <filesystem>

namespace
{
namespace fs = std::filesystem;

bool RemoveFileIfPresent(const fs::path& FilePath, MString& OutError)
{
    std::error_code Ec;
    const bool bRemoved = fs::remove(FilePath, Ec);
    if (Ec)
    {
        OutError = "editor_asset_delete_failed:" + FilePath.generic_string();
        return false;
    }

    if (!bRemoved && fs::exists(FilePath))
    {
        OutError = "editor_asset_delete_failed:" + FilePath.generic_string();
        return false;
    }

    return true;
}

bool SaveMonsterConfigDocument(
    const FEditorAssetIdentity& AssetId,
    const FMonsterConfigEditorModel& Model,
    MString& OutError,
    TUniquePtr<MMonsterConfigDocument>* OutSavedDocument = nullptr)
{
    TUniquePtr<MMonsterConfigDocument> Document = std::make_unique<MMonsterConfigDocument>();
    if (!Document->CreateNew(AssetId, OutError))
    {
        return false;
    }

    Document->SetModel(Model);
    if (!Document->Save(OutError))
    {
        return false;
    }

    if (OutSavedDocument)
    {
        *OutSavedDocument = std::move(Document);
    }

    return true;
}

bool TryParseIdentityFromSourcePath(const MString& SourcePath, FEditorAssetIdentity& OutIdentity, MString& OutError)
{
    return MEditorPaths::TryParseMonsterConfigIdentityFromSourcePath(SourcePath, OutIdentity, &OutError);
}

bool RemoveGeneratedOutputsForSourcePath(const MString& SourcePath, MString& OutError)
{
    FEditorAssetIdentity Identity;
    if (!TryParseIdentityFromSourcePath(SourcePath, Identity, OutError))
    {
        return false;
    }

    const FEditorAssetPathSet Paths = MEditorPaths::BuildMonsterConfigPaths(Identity);
    const TVector<MString> GeneratedPaths = {
        Paths.ExportJsonPath,
        Paths.ExportMobPath,
        Paths.ExportRoundTripPath,
        Paths.PublishMobPath,
    };

    for (const MString& FilePath : GeneratedPaths)
    {
        if (!RemoveFileIfPresent(fs::path(FilePath), OutError))
        {
            return false;
        }
    }

    return true;
}
}

bool MObjectEditorServiceApp::Initialize(MString& OutError)
{
    (void)OutError;
    return true;
}

TVector<FEditorAssetIdentity> MObjectEditorServiceApp::ListMonsterConfigs(MString& OutError) const
{
    (void)OutError;

    TVector<FEditorAssetIdentity> Assets;
    const fs::path RootPath("EditorAssets");
    if (!fs::exists(RootPath))
    {
        return Assets;
    }

    std::error_code Ec;
    for (fs::recursive_directory_iterator It(RootPath, Ec), End; It != End && !Ec; It.increment(Ec))
    {
        if (!It->is_regular_file())
        {
            continue;
        }

        const MString SourcePath = It->path().generic_string();
        FEditorAssetIdentity AssetId;
        if (MEditorPaths::TryParseMonsterConfigIdentityFromSourcePath(SourcePath, AssetId))
        {
            Assets.push_back(std::move(AssetId));
        }
    }

    std::sort(
        Assets.begin(),
        Assets.end(),
        [](const FEditorAssetIdentity& Left, const FEditorAssetIdentity& Right)
        {
            return Left.SourcePath < Right.SourcePath;
        });

    return Assets;
}

bool MObjectEditorServiceApp::CreateNewMonsterConfig(const FEditorAssetIdentity& AssetId, MString& OutError)
{
    CurrentMonsterConfigDocument = std::make_unique<MMonsterConfigDocument>();
    return CurrentMonsterConfigDocument->CreateNew(AssetId, OutError);
}

bool MObjectEditorServiceApp::OpenMonsterConfig(const MString& FilePath, MString& OutError)
{
    CurrentMonsterConfigDocument = std::make_unique<MMonsterConfigDocument>();
    return CurrentMonsterConfigDocument->LoadFromFile(FilePath, OutError);
}

bool MObjectEditorServiceApp::SaveMonsterConfig(
    const FEditorAssetIdentity& AssetId,
    const FMonsterConfigEditorModel& Model,
    MString& OutError)
{
    return SaveMonsterConfig(AssetId, Model, MString{}, OutError);
}

bool MObjectEditorServiceApp::SaveMonsterConfig(
    const FEditorAssetIdentity& AssetId,
    const FMonsterConfigEditorModel& Model,
    const MString& PreviousSourcePath,
    MString& OutError)
{
    const FEditorAssetPathSet TargetPaths = MEditorPaths::BuildMonsterConfigPaths(AssetId);
    if (!PreviousSourcePath.empty() && PreviousSourcePath != TargetPaths.SourcePath)
    {
        const fs::path PreviousPath(PreviousSourcePath);
        if (!fs::exists(PreviousPath))
        {
            OutError = "editor_asset_rename_missing_source:" + PreviousSourcePath;
            return false;
        }

        const fs::path TargetSourcePath(TargetPaths.SourcePath);
        if (fs::exists(TargetSourcePath))
        {
            OutError = "editor_asset_save_target_exists:" + TargetPaths.SourcePath;
            return false;
        }
    }

    if (!SaveMonsterConfigDocument(AssetId, Model, OutError, &CurrentMonsterConfigDocument))
    {
        return false;
    }

    if (!PreviousSourcePath.empty() && PreviousSourcePath != TargetPaths.SourcePath)
    {
        FEditorAssetIdentity PreviousIdentity;
        MString ParseError;
        if (!MEditorPaths::TryParseMonsterConfigIdentityFromSourcePath(PreviousSourcePath, PreviousIdentity, &ParseError))
        {
            OutError = ParseError;
            return false;
        }

        const FEditorAssetPathSet PreviousPaths = MEditorPaths::BuildMonsterConfigPaths(PreviousIdentity);
        const TVector<MString> OldPaths = {
            PreviousPaths.SourcePath,
            PreviousPaths.ExportJsonPath,
            PreviousPaths.ExportMobPath,
            PreviousPaths.ExportRoundTripPath,
            PreviousPaths.PublishMobPath,
        };

        for (const MString& FilePath : OldPaths)
        {
            if (FilePath == TargetPaths.SourcePath ||
                FilePath == TargetPaths.ExportJsonPath ||
                FilePath == TargetPaths.ExportMobPath ||
                FilePath == TargetPaths.ExportRoundTripPath ||
                FilePath == TargetPaths.PublishMobPath)
            {
                continue;
            }

            if (!RemoveFileIfPresent(fs::path(FilePath), OutError))
            {
                return false;
            }
        }
    }

    return true;
}

TVector<FMonsterConfigSaveResult> MObjectEditorServiceApp::SaveMonsterConfigsBatch(
    const TVector<FMonsterConfigSaveRequest>& Requests,
    bool& bOutHadFailures,
    MString& OutError)
{
    bOutHadFailures = false;
    OutError.clear();

    TVector<FMonsterConfigSaveResult> Results;
    Results.resize(Requests.size());

    TMap<MString, size_t> TargetIndexByPath;
    TSet<MString> PreviousSourcePaths;

    for (size_t Index = 0; Index < Requests.size(); ++Index)
    {
        Results[Index].PreviousSourcePath = Requests[Index].PreviousSourcePath;
        Results[Index].SourcePath = MEditorPaths::BuildMonsterConfigPaths(Requests[Index].Identity).SourcePath;

        const auto [It, bInserted] = TargetIndexByPath.emplace(Results[Index].SourcePath, Index);
        if (!bInserted)
        {
            Results[Index].Error = "editor_asset_duplicate_target:" + Results[Index].SourcePath;
            Results[It->second].Error = "editor_asset_duplicate_target:" + Results[Index].SourcePath;
            bOutHadFailures = true;
        }

        if (!Requests[Index].PreviousSourcePath.empty() &&
            Requests[Index].PreviousSourcePath != Results[Index].SourcePath)
        {
            PreviousSourcePaths.insert(Requests[Index].PreviousSourcePath);
        }
    }

    TUniquePtr<MMonsterConfigDocument> LastSavedDocument;
    for (size_t Index = 0; Index < Requests.size(); ++Index)
    {
        FMonsterConfigSaveResult& Result = Results[Index];
        if (!Result.Error.empty())
        {
            continue;
        }

        const FMonsterConfigSaveRequest& Request = Requests[Index];
        if (!Request.PreviousSourcePath.empty() &&
            Request.PreviousSourcePath != Result.SourcePath &&
            !fs::exists(fs::path(Request.PreviousSourcePath)))
        {
            Result.Error = "editor_asset_rename_missing_source:" + Request.PreviousSourcePath;
            bOutHadFailures = true;
            continue;
        }

        const fs::path TargetPath(Result.SourcePath);
        if (fs::exists(TargetPath))
        {
            const bool bIsOverwriteSelf = !Request.PreviousSourcePath.empty() && Request.PreviousSourcePath == Result.SourcePath;
            const bool bIsRenameTargetFreedByBatch = PreviousSourcePaths.find(Result.SourcePath) != PreviousSourcePaths.end();
            if (!bIsOverwriteSelf && !bIsRenameTargetFreedByBatch)
            {
                Result.Error = "editor_asset_save_target_exists:" + Result.SourcePath;
                bOutHadFailures = true;
                continue;
            }
        }

        MString SaveError;
        TUniquePtr<MMonsterConfigDocument> SavedDocument;
        if (!SaveMonsterConfigDocument(Request.Identity, Request.Model, SaveError, &SavedDocument))
        {
            Result.Error = SaveError;
            bOutHadFailures = true;
            continue;
        }

        Result.bOk = true;
        LastSavedDocument = std::move(SavedDocument);
    }

    if (bOutHadFailures)
    {
        return Results;
    }

    TSet<MString> FinalTargetPaths;
    for (const FMonsterConfigSaveResult& Result : Results)
    {
        if (Result.bOk)
        {
            FinalTargetPaths.insert(Result.SourcePath);
        }
    }

    for (size_t Index = 0; Index < Requests.size(); ++Index)
    {
        const FMonsterConfigSaveRequest& Request = Requests[Index];
        const FMonsterConfigSaveResult& Result = Results[Index];
        if (!Result.bOk ||
            Request.PreviousSourcePath.empty() ||
            Request.PreviousSourcePath == Result.SourcePath)
        {
            continue;
        }

        MString CleanupError;
        if (!RemoveGeneratedOutputsForSourcePath(Request.PreviousSourcePath, CleanupError))
        {
            Results[Index].bOk = false;
            Results[Index].Error = CleanupError;
            bOutHadFailures = true;
            continue;
        }

        if (FinalTargetPaths.find(Request.PreviousSourcePath) == FinalTargetPaths.end())
        {
            if (!RemoveFileIfPresent(fs::path(Request.PreviousSourcePath), CleanupError))
            {
                Results[Index].bOk = false;
                Results[Index].Error = CleanupError;
                bOutHadFailures = true;
            }
        }
    }

    if (!bOutHadFailures && LastSavedDocument)
    {
        CurrentMonsterConfigDocument = std::move(LastSavedDocument);
    }

    return Results;
}

bool MObjectEditorServiceApp::DeleteMonsterConfig(const FEditorAssetIdentity& AssetId, MString& OutError)
{
    const FEditorAssetPathSet Paths = MEditorPaths::BuildMonsterConfigPaths(AssetId);
    const fs::path SourcePath(Paths.SourcePath);
    if (!fs::exists(SourcePath))
    {
        OutError = "editor_asset_delete_missing:" + Paths.SourcePath;
        return false;
    }

    if (!RemoveFileIfPresent(SourcePath, OutError))
    {
        return false;
    }

    const TVector<MString> GeneratedPaths = {
        Paths.ExportJsonPath,
        Paths.ExportMobPath,
        Paths.ExportRoundTripPath,
        Paths.PublishMobPath,
    };
    for (const MString& FilePath : GeneratedPaths)
    {
        if (!RemoveFileIfPresent(fs::path(FilePath), OutError))
        {
            return false;
        }
    }

    if (CurrentMonsterConfigDocument &&
        CurrentMonsterConfigDocument->GetIdentity().SourcePath == Paths.SourcePath)
    {
        CurrentMonsterConfigDocument.reset();
    }

    return true;
}

bool MObjectEditorServiceApp::SaveCurrentDocument(MString& OutError)
{
    if (!CurrentMonsterConfigDocument)
    {
        OutError = "editor_document_missing";
        return false;
    }
    return CurrentMonsterConfigDocument->Save(OutError);
}

TVector<FValidationIssue> MObjectEditorServiceApp::ValidateMonsterConfig(const FMonsterConfigEditorModel& Model) const
{
    return MMonsterConfigValidator::Validate(Model);
}

TVector<FValidationIssue> MObjectEditorServiceApp::ValidateCurrentDocument() const
{
    if (!CurrentMonsterConfigDocument)
    {
        return {};
    }
    return MMonsterConfigValidator::Validate(CurrentMonsterConfigDocument->GetModel());
}

FAssetExportResult MObjectEditorServiceApp::ExportMonsterConfig(
    const FEditorAssetIdentity& AssetId,
    const FMonsterConfigEditorModel& Model,
    const FAssetExportOptions& Options) const
{
    return MAssetExportService::ExportMonsterConfig(AssetId, Model, Options);
}

FAssetExportResult MObjectEditorServiceApp::ExportCurrentDocument(const FAssetExportOptions& Options) const
{
    if (!CurrentMonsterConfigDocument)
    {
        FAssetExportResult Result;
        Result.Error = "editor_document_missing";
        return Result;
    }

    return MAssetExportService::ExportMonsterConfig(
        CurrentMonsterConfigDocument->GetIdentity(),
        CurrentMonsterConfigDocument->GetModel(),
        Options);
}
