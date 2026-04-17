#include "Tools/MObjectEditorService/Core/EditorPaths.h"

#include <filesystem>

namespace
{
namespace fs = std::filesystem;

MString JoinSegments(const TVector<MString>& Segments)
{
    MString Result;
    for (size_t Index = 0; Index < Segments.size(); ++Index)
    {
        if (Index > 0)
        {
            Result += "/";
        }
        Result += Segments[Index];
    }
    return Result;
}

MString BuildRelativePath(const MString& RootDir, const MString& CategoryPath, const MString& FileName)
{
    fs::path Result = RootDir;
    if (!CategoryPath.empty())
    {
        Result /= fs::path(CategoryPath);
    }
    Result /= FileName;
    return Result.generic_string();
}
}

MString MEditorPaths::NormalizeCategoryPath(const MString& CategoryPath)
{
    TVector<MString> Segments;
    MString Current;
    for (char Ch : CategoryPath)
    {
        const char Normalized = (Ch == '\\') ? '/' : Ch;
        if (Normalized == '/')
        {
            if (!Current.empty())
            {
                Segments.push_back(Current);
                Current.clear();
            }
            continue;
        }
        Current.push_back(Normalized);
    }

    if (!Current.empty())
    {
        Segments.push_back(Current);
    }

    return JoinSegments(Segments);
}

FEditorAssetPathSet MEditorPaths::BuildMonsterConfigPaths(const FEditorAssetIdentity& AssetId)
{
    const MString NormalizedCategory = NormalizeCategoryPath(AssetId.CategoryPath);
    const MString SourceFile = AssetId.AssetName + ".masset.json";
    const MString JsonFile = AssetId.AssetName + ".json";
    const MString MobFile = AssetId.AssetName + ".mob";
    const MString RoundTripFile = AssetId.AssetName + ".roundtrip.json";

    FEditorAssetPathSet Paths;
    Paths.SourcePath = BuildRelativePath("EditorAssets", NormalizedCategory, SourceFile);
    Paths.ExportJsonPath = BuildRelativePath("Build/Generated/Assets", NormalizedCategory, JsonFile);
    Paths.ExportMobPath = BuildRelativePath("Build/Generated/Assets", NormalizedCategory, MobFile);
    Paths.ExportRoundTripPath = BuildRelativePath("Build/Generated/Assets", NormalizedCategory, RoundTripFile);
    Paths.PublishMobPath = BuildRelativePath("GameData", NormalizedCategory, MobFile);
    return Paths;
}

bool MEditorPaths::TryParseMonsterConfigIdentityFromSourcePath(const MString& SourcePath, FEditorAssetIdentity& OutIdentity, MString* OutError)
{
    const fs::path InputPath = fs::path(SourcePath).lexically_normal();

    TVector<MString> Segments;
    for (const fs::path& Part : InputPath)
    {
        const MString Segment = Part.generic_string();
        if (!Segment.empty() && Segment != "/")
        {
            Segments.push_back(Segment);
        }
    }

    size_t EditorAssetsIndex = Segments.size();
    for (size_t Index = 0; Index < Segments.size(); ++Index)
    {
        if (Segments[Index] == "EditorAssets")
        {
            EditorAssetsIndex = Index;
            break;
        }
    }

    if (EditorAssetsIndex == Segments.size() || EditorAssetsIndex + 1 >= Segments.size())
    {
        if (OutError)
        {
            *OutError = "editor_asset_path_invalid_root:" + SourcePath;
        }
        return false;
    }

    const MString FileName = Segments.back();
    constexpr TStringView Suffix = ".masset.json";
    if (FileName.size() <= Suffix.size() || FileName.substr(FileName.size() - Suffix.size()) != Suffix)
    {
        if (OutError)
        {
            *OutError = "editor_asset_path_invalid_extension:" + SourcePath;
        }
        return false;
    }

    TVector<MString> CategorySegments;
    for (size_t Index = EditorAssetsIndex + 1; Index + 1 < Segments.size(); ++Index)
    {
        CategorySegments.push_back(Segments[Index]);
    }

    OutIdentity.SourcePath = InputPath.generic_string();
    OutIdentity.CategoryPath = JoinSegments(CategorySegments);
    OutIdentity.AssetName = FileName.substr(0, FileName.size() - Suffix.size());
    return true;
}
