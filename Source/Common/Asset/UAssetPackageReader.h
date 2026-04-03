#pragma once

#include "Common/Runtime/MLib.h"

struct FUAssetPackageSummary
{
    uint32 Tag = 0;
    int32 LegacyFileVersion = 0;
    int32 LegacyUE3Version = 0;
    int32 FileVersionUE4 = 0;
    int32 FileVersionUE5 = 0;
    int32 FileVersionLicenseeUE = 0;
    int32 TotalHeaderSize = 0;
    MString PackageName;
    uint32 PackageFlags = 0;
    int32 NameCount = 0;
    int32 NameOffset = 0;
    bool bHasSoftObjectPathList = false;
    int32 SoftObjectPathCount = 0;
    int32 SoftObjectPathOffset = 0;
    MString LocalizationId;
    int32 GatherableTextDataCount = 0;
    int32 GatherableTextDataOffset = 0;
    int32 ExportCount = 0;
    int32 ExportOffset = 0;
    int32 ImportCount = 0;
    int32 ImportOffset = 0;
    int32 DependsOffset = 0;
    TVector<int32> TableOffsetCandidates;
};

struct FUAssetNameEntry
{
    MString Name;
    uint16 NonCasePreservingHash = 0;
    uint16 CasePreservingHash = 0;
    bool bHasHashes = false;
};

struct FUAssetImportEntry
{
    int32 ClassPackageNameIndex = -1;
    int32 ClassPackageNameNumber = 0;
    int32 ClassNameIndex = -1;
    int32 ClassNameNumber = 0;
    int32 PackageNameIndex = -1;
    int32 PackageNameNumber = 0;
    int32 OuterIndex = 0;
    int32 ObjectNameIndex = -1;
    int32 ObjectNameNumber = 0;
    bool bImportOptional = false;
    uint32 RowSize = 0;

    MString ClassPackageName;
    MString ClassName;
    MString PackageName;
    MString ObjectName;
};

struct FUAssetExportEntry
{
    int32 ClassIndex = 0;
    int32 SuperIndex = 0;
    int32 TemplateIndex = 0;
    int32 OuterIndex = 0;
    int32 ObjectNameIndex = -1;
    int32 ObjectNameNumber = 0;
    uint32 ObjectFlags = 0;
    int64 SerialSize = 0;
    int64 SerialOffset = 0;
    bool bSerialDataMayLiveOutsideUAsset = false;
    uint32 RowSize = 0;

    MString ObjectName;
    MString ClassObjectName;
    MString SuperObjectName;
    MString OuterObjectName;
};

struct FUAssetPackage
{
    MString FilePath;
    uint64 FileSize = 0;
    TByteArray FileData;
    FUAssetPackageSummary Summary;
    TVector<FUAssetNameEntry> Names;
    TVector<FUAssetImportEntry> Imports;
    TVector<FUAssetExportEntry> Exports;
};

class FUAssetPackageReader
{
public:
    static bool LoadFromFile(const MString& FilePath, FUAssetPackage& OutPackage, MString& OutError);

    static MString ResolveNameReference(const FUAssetPackage& Package, int32 NameIndex, int32 NameNumber = 0);
    static MString ResolvePackageObjectName(const FUAssetPackage& Package, int32 PackageIndex);
};
