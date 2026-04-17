#include "Tools/MObjectEditorService/Export/AssetExportService.h"

#include "Common/Runtime/Asset/MObjectAssetCompiler.h"
#include "Common/Runtime/Asset/MObjectAssetJson.h"
#include "Common/Runtime/Asset/MObjectAssetLoader.h"
#include "Common/Runtime/Object/Object.h"
#include "Servers/Scene/Combat/MonsterConfig.h"
#include "Tools/MObjectEditorService/Core/EditorPaths.h"
#include "Tools/MObjectEditorService/Models/MonsterConfigEditorModel.h"
#include "Tools/MObjectEditorService/Validation/MonsterConfigValidator.h"

#include <filesystem>

namespace
{
namespace fs = std::filesystem;

bool HasValidationErrors(const TVector<FValidationIssue>& Issues)
{
    for (const FValidationIssue& Issue : Issues)
    {
        if (Issue.Severity == EValidationSeverity::Error)
        {
            return true;
        }
    }
    return false;
}

void RemoveIfExists(const MString& FilePath)
{
    std::error_code Ec;
    fs::remove(fs::path(FilePath), Ec);
}

bool EnsureParentDirectory(const MString& FilePath, MString& OutError)
{
    std::error_code Ec;
    fs::create_directories(fs::path(FilePath).parent_path(), Ec);
    if (Ec)
    {
        OutError = "asset_export_create_parent_failed:" + FilePath;
        return false;
    }
    return true;
}

bool WriteTextFile(const MString& FilePath, const MString& Text, MString& OutError)
{
    if (!EnsureParentDirectory(FilePath, OutError))
    {
        return false;
    }

    TOfstream Output(FilePath, std::ios::binary | std::ios::trunc);
    if (!Output.is_open())
    {
        OutError = "asset_export_open_text_failed:" + FilePath;
        return false;
    }

    Output.write(Text.data(), static_cast<std::streamsize>(Text.size()));
    if (!Output.good())
    {
        OutError = "asset_export_write_text_failed:" + FilePath;
        return false;
    }
    return true;
}

bool WriteBytesFile(const MString& FilePath, const TByteArray& Bytes, MString& OutError)
{
    if (!EnsureParentDirectory(FilePath, OutError))
    {
        return false;
    }

    TOfstream Output(FilePath, std::ios::binary | std::ios::trunc);
    if (!Output.is_open())
    {
        OutError = "asset_export_open_bytes_failed:" + FilePath;
        return false;
    }

    if (!Bytes.empty())
    {
        Output.write(reinterpret_cast<const char*>(Bytes.data()), static_cast<std::streamsize>(Bytes.size()));
    }
    if (!Output.good())
    {
        OutError = "asset_export_write_bytes_failed:" + FilePath;
        return false;
    }
    return true;
}
}

FAssetExportResult MAssetExportService::ExportMonsterConfig(
    const FEditorAssetIdentity& AssetId,
    const FMonsterConfigEditorModel& Model,
    const FAssetExportOptions& Options)
{
    FAssetExportResult Result;
    Result.Issues = MMonsterConfigValidator::Validate(Model);
    if (HasValidationErrors(Result.Issues))
    {
        Result.Error = "monster_config_validation_failed";
        return Result;
    }

    const FEditorAssetPathSet Paths = MEditorPaths::BuildMonsterConfigPaths(AssetId);
    RemoveIfExists(Paths.ExportJsonPath);
    RemoveIfExists(Paths.ExportMobPath);
    RemoveIfExists(Paths.ExportRoundTripPath);

    MMonsterConfig* RuntimeConfig = nullptr;
    if (!MMonsterConfigModelConverter::BuildRuntimeObject(AssetId, Model, RuntimeConfig, Result.Error))
    {
        return Result;
    }

    MString ExportJson;
    if ((Options.bExportJson || Options.bExportRoundTripJson) &&
        !MObjectAssetJson::ExportAssetObjectToJson(RuntimeConfig, ExportJson, &Result.Error))
    {
        DestroyMObject(RuntimeConfig);
        return Result;
    }

    if (Options.bExportJson)
    {
        if (!WriteTextFile(Paths.ExportJsonPath, ExportJson, Result.Error))
        {
            DestroyMObject(RuntimeConfig);
            return Result;
        }
        Result.JsonPath = Paths.ExportJsonPath;
    }

    TByteArray MobBytes;
    if (Options.bExportMob || Options.bExportRoundTripJson || Options.bPublishMob)
    {
        if (!MObjectAssetCompiler::CompileBytesFromObject(RuntimeConfig, MobBytes, &Result.Error))
        {
            DestroyMObject(RuntimeConfig);
            return Result;
        }
    }

    if (Options.bExportMob)
    {
        if (!WriteBytesFile(Paths.ExportMobPath, MobBytes, Result.Error))
        {
            DestroyMObject(RuntimeConfig);
            return Result;
        }
        Result.MobPath = Paths.ExportMobPath;
    }

    if (Options.bExportRoundTripJson)
    {
        MObject* LoadedObject = MObjectAssetLoader::LoadFromBytes(MobBytes, nullptr, &Result.Error);
        if (!LoadedObject)
        {
            DestroyMObject(RuntimeConfig);
            return Result;
        }

        MString RoundTripJson;
        const bool bRoundTripOk = MObjectAssetJson::ExportAssetObjectToJson(LoadedObject, RoundTripJson, &Result.Error);
        DestroyMObject(LoadedObject);
        if (!bRoundTripOk)
        {
            DestroyMObject(RuntimeConfig);
            return Result;
        }

        if (!WriteTextFile(Paths.ExportRoundTripPath, RoundTripJson, Result.Error))
        {
            DestroyMObject(RuntimeConfig);
            return Result;
        }
        Result.RoundTripPath = Paths.ExportRoundTripPath;
    }

    if (Options.bPublishMob)
    {
        RemoveIfExists(Paths.PublishMobPath);
        if (!WriteBytesFile(Paths.PublishMobPath, MobBytes, Result.Error))
        {
            DestroyMObject(RuntimeConfig);
            return Result;
        }
        Result.PublishPath = Paths.PublishMobPath;
    }

    DestroyMObject(RuntimeConfig);
    Result.bSuccess = true;
    return Result;
}
