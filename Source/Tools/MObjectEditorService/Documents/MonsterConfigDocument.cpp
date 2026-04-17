#include "Tools/MObjectEditorService/Documents/MonsterConfigDocument.h"

#include "Common/Runtime/Asset/MObjectAssetJson.h"
#include "Common/Runtime/Object/Object.h"
#include "Servers/Scene/Combat/MonsterConfig.h"
#include "Tools/MObjectEditorService/Core/EditorPaths.h"
#include "Tools/MObjectEditorService/Models/MonsterConfigEditorModel.h"

#include <filesystem>
#include <iterator>

namespace
{
namespace fs = std::filesystem;
}

bool MMonsterConfigDocument::CreateNew(const FEditorAssetIdentity& AssetId, MString& OutError)
{
    if (AssetId.AssetName.empty())
    {
        OutError = "editor_asset_name_required";
        return false;
    }

    Identity = AssetId;
    if (Identity.SourcePath.empty())
    {
        Identity.SourcePath = MEditorPaths::BuildMonsterConfigPaths(Identity).SourcePath;
    }

    Model = FMonsterConfigEditorModel{};
    bDirty = false;
    return true;
}

bool MMonsterConfigDocument::ReadTextFile(const MString& FilePath, MString& OutText, MString& OutError)
{
    TIfstream Input(FilePath, std::ios::binary);
    if (!Input.is_open())
    {
        OutError = "editor_asset_open_failed:" + FilePath;
        return false;
    }

    OutText.assign(std::istreambuf_iterator<char>(Input), std::istreambuf_iterator<char>());
    return true;
}

bool MMonsterConfigDocument::WriteTextFile(const MString& FilePath, const MString& Text, MString& OutError)
{
    std::error_code Ec;
    fs::create_directories(fs::path(FilePath).parent_path(), Ec);
    if (Ec)
    {
        OutError = "editor_asset_create_parent_failed:" + FilePath;
        return false;
    }

    TOfstream Output(FilePath, std::ios::binary | std::ios::trunc);
    if (!Output.is_open())
    {
        OutError = "editor_asset_write_open_failed:" + FilePath;
        return false;
    }

    Output.write(Text.data(), static_cast<std::streamsize>(Text.size()));
    if (!Output.good())
    {
        OutError = "editor_asset_write_failed:" + FilePath;
        return false;
    }

    return true;
}

bool MMonsterConfigDocument::LoadFromFile(const MString& FilePath, MString& OutError)
{
    MString Text;
    if (!ReadTextFile(FilePath, Text, OutError))
    {
        return false;
    }

    MString IdentityError;
    if (!MEditorPaths::TryParseMonsterConfigIdentityFromSourcePath(FilePath, Identity, &IdentityError))
    {
        OutError = IdentityError;
        return false;
    }

    MObject* LoadedObject = MObjectAssetJson::ImportAssetObjectFromJson(Text, nullptr, &OutError);
    if (!LoadedObject)
    {
        return false;
    }

    if (LoadedObject->GetClass() != MMonsterConfig::StaticClass())
    {
        OutError = "editor_asset_type_mismatch:" + FilePath;
        DestroyMObject(LoadedObject);
        return false;
    }

    MString ConvertError;
    const bool bOk = MMonsterConfigModelConverter::BuildEditorModel(
        *static_cast<MMonsterConfig*>(LoadedObject),
        Model,
        ConvertError);
    DestroyMObject(LoadedObject);
    if (!bOk)
    {
        OutError = ConvertError;
        return false;
    }

    bDirty = false;
    return true;
}

bool MMonsterConfigDocument::SaveToFile(const MString& FilePath, MString& OutError)
{
    FEditorAssetIdentity SaveIdentity = Identity;
    if (!FilePath.empty())
    {
        MString IdentityError;
        if (!MEditorPaths::TryParseMonsterConfigIdentityFromSourcePath(FilePath, SaveIdentity, &IdentityError))
        {
            OutError = IdentityError;
            return false;
        }
    }
    else if (SaveIdentity.SourcePath.empty())
    {
        SaveIdentity.SourcePath = MEditorPaths::BuildMonsterConfigPaths(SaveIdentity).SourcePath;
    }

    MMonsterConfig* RuntimeConfig = nullptr;
    if (!MMonsterConfigModelConverter::BuildRuntimeObject(SaveIdentity, Model, RuntimeConfig, OutError))
    {
        return false;
    }

    MString JsonText;
    const bool bExported = MObjectAssetJson::ExportAssetObjectToJson(RuntimeConfig, JsonText, &OutError);
    DestroyMObject(RuntimeConfig);
    if (!bExported)
    {
        return false;
    }

    if (!WriteTextFile(SaveIdentity.SourcePath, JsonText, OutError))
    {
        return false;
    }

    Identity = SaveIdentity;
    bDirty = false;
    return true;
}

bool MMonsterConfigDocument::Save(MString& OutError)
{
    return SaveToFile(Identity.SourcePath, OutError);
}

void MMonsterConfigDocument::SetModel(const FMonsterConfigEditorModel& InModel, bool bMarkDirty)
{
    Model = InModel;
    bDirty = bMarkDirty;
}
