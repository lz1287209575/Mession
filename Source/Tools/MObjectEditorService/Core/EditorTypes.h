#pragma once

#include "Common/Runtime/MLib.h"

struct FEditorAssetIdentity
{
    MString AssetName;
    MString CategoryPath;
    MString SourcePath;
};

struct FEditorAssetPathSet
{
    MString SourcePath;
    MString ExportJsonPath;
    MString ExportMobPath;
    MString ExportRoundTripPath;
    MString PublishMobPath;
};

enum class EValidationSeverity : uint8
{
    Info = 0,
    Warning = 1,
    Error = 2,
};

struct FValidationIssue
{
    EValidationSeverity Severity = EValidationSeverity::Error;
    MString FieldPath;
    MString Code;
    MString Message;
};
