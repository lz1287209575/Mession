#pragma once

#include "Tools/MObjectEditorService/Core/EditorTypes.h"

class MEditorPaths
{
public:
    static MString NormalizeCategoryPath(const MString& CategoryPath);
    static FEditorAssetPathSet BuildMonsterConfigPaths(const FEditorAssetIdentity& AssetId);
    static bool TryParseMonsterConfigIdentityFromSourcePath(const MString& SourcePath, FEditorAssetIdentity& OutIdentity, MString* OutError = nullptr);
};
