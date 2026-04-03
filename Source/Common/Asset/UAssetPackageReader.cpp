#include "Common/Asset/UAssetPackageReader.h"

#include <cstring>

namespace
{
constexpr uint32 UE_PACKAGE_FILE_TAG = 0x9E2A83C1u;
constexpr uint32 MAX_CUSTOM_VERSION_COUNT = 4096;
constexpr uint32 MAX_NAME_COUNT = 1u << 20;
constexpr uint32 MAX_IMPORT_EXPORT_COUNT = 1u << 18;

class FByteReader
{
public:
    explicit FByteReader(const TByteArray& InData)
        : Data(InData)
    {
    }

    template<typename T>
    bool Read(T& OutValue)
    {
        static_assert(std::is_trivially_copyable_v<T>);

        if (Offset + sizeof(T) > Data.size())
        {
            return false;
        }

        std::memcpy(&OutValue, Data.data() + Offset, sizeof(T));
        Offset += sizeof(T);
        return true;
    }

    bool ReadString(MString& OutValue)
    {
        int32 SerializedLength = 0;
        if (!Read(SerializedLength))
        {
            return false;
        }

        if (SerializedLength == 0)
        {
            OutValue.clear();
            return true;
        }

        if (SerializedLength > 0)
        {
            const size_t ByteCount = static_cast<size_t>(SerializedLength);
            if (Offset + ByteCount > Data.size())
            {
                return false;
            }

            OutValue.assign(
                reinterpret_cast<const char*>(Data.data() + Offset),
                reinterpret_cast<const char*>(Data.data() + Offset + ByteCount));
            Offset += ByteCount;

            if (!OutValue.empty() && OutValue.back() == '\0')
            {
                OutValue.pop_back();
            }
            return true;
        }

        const size_t WideCharCount = static_cast<size_t>(-SerializedLength);
        const size_t ByteCount = WideCharCount * sizeof(uint16);
        if (Offset + ByteCount > Data.size())
        {
            return false;
        }

        OutValue.clear();
        OutValue.reserve(WideCharCount);
        for (size_t Index = 0; Index < WideCharCount; ++Index)
        {
            uint16 CodeUnit = 0;
            std::memcpy(&CodeUnit, Data.data() + Offset + (Index * sizeof(uint16)), sizeof(uint16));
            if (CodeUnit == 0)
            {
                continue;
            }

            if (CodeUnit <= 0x7F)
            {
                OutValue.push_back(static_cast<char>(CodeUnit));
                continue;
            }

            if (CodeUnit <= 0x7FF)
            {
                OutValue.push_back(static_cast<char>(0xC0 | (CodeUnit >> 6)));
                OutValue.push_back(static_cast<char>(0x80 | (CodeUnit & 0x3F)));
                continue;
            }

            OutValue.push_back(static_cast<char>(0xE0 | (CodeUnit >> 12)));
            OutValue.push_back(static_cast<char>(0x80 | ((CodeUnit >> 6) & 0x3F)));
            OutValue.push_back(static_cast<char>(0x80 | (CodeUnit & 0x3F)));
        }

        Offset += ByteCount;
        return true;
    }

    bool Skip(size_t ByteCount)
    {
        if (Offset + ByteCount > Data.size())
        {
            return false;
        }

        Offset += ByteCount;
        return true;
    }

    bool Seek(size_t NewOffset)
    {
        if (NewOffset > Data.size())
        {
            return false;
        }

        Offset = NewOffset;
        return true;
    }

    size_t Tell() const
    {
        return Offset;
    }

    size_t Size() const
    {
        return Data.size();
    }

private:
    const TByteArray& Data;
    size_t Offset = 0;
};

struct FNameReference
{
    int32 Index = -1;
    int32 Number = 0;
};

bool ReadNameReference(FByteReader& Reader, FNameReference& OutReference)
{
    return Reader.Read(OutReference.Index) && Reader.Read(OutReference.Number);
}

bool SkipCustomVersions(FByteReader& Reader)
{
    int32 CustomVersionCount = 0;
    if (!Reader.Read(CustomVersionCount))
    {
        return false;
    }

    if (CustomVersionCount < 0 || static_cast<uint32>(CustomVersionCount) > MAX_CUSTOM_VERSION_COUNT)
    {
        return false;
    }

    return Reader.Skip(static_cast<size_t>(CustomVersionCount) * (sizeof(uint32) * 5));
}

bool IsReasonableOffset(int32 Offset, size_t FileSize)
{
    return Offset >= 0 && static_cast<size_t>(Offset) <= FileSize;
}

bool IsReasonableCount(int32 Count, uint32 MaxCount)
{
    return Count >= 0 && static_cast<uint32>(Count) <= MaxCount;
}

bool ValidateSummary(const FUAssetPackageSummary& Summary, size_t FileSize)
{
    if (Summary.Tag != UE_PACKAGE_FILE_TAG)
    {
        return false;
    }

    if (!IsReasonableOffset(Summary.TotalHeaderSize, FileSize) || Summary.TotalHeaderSize <= 0)
    {
        return false;
    }

    if (!IsReasonableCount(Summary.NameCount, MAX_NAME_COUNT) ||
        !IsReasonableCount(Summary.ExportCount, MAX_IMPORT_EXPORT_COUNT) ||
        !IsReasonableCount(Summary.ImportCount, MAX_IMPORT_EXPORT_COUNT))
    {
        return false;
    }

    if (!IsReasonableOffset(Summary.NameOffset, FileSize) ||
        !IsReasonableOffset(Summary.ExportOffset, FileSize) ||
        !IsReasonableOffset(Summary.ImportOffset, FileSize) ||
        !IsReasonableOffset(Summary.DependsOffset, FileSize))
    {
        return false;
    }

    if (Summary.NameCount > 0 && Summary.NameOffset <= 0)
    {
        return false;
    }

    if (Summary.ExportCount > 0 && Summary.ExportOffset <= 0)
    {
        return false;
    }

    if (Summary.ImportCount > 0 && Summary.ImportOffset <= 0)
    {
        return false;
    }

    return true;
}

bool TryReadSummaryLayout(FByteReader& Reader, bool bHasSoftObjectPathList, FUAssetPackageSummary& InOutSummary)
{
    InOutSummary.bHasSoftObjectPathList = bHasSoftObjectPathList;
    InOutSummary.SoftObjectPathCount = 0;
    InOutSummary.SoftObjectPathOffset = 0;
    InOutSummary.LocalizationId.clear();
    InOutSummary.GatherableTextDataCount = 0;
    InOutSummary.GatherableTextDataOffset = 0;
    InOutSummary.ExportCount = 0;
    InOutSummary.ExportOffset = 0;
    InOutSummary.ImportCount = 0;
    InOutSummary.ImportOffset = 0;
    InOutSummary.DependsOffset = 0;

    if (bHasSoftObjectPathList)
    {
        if (!Reader.Read(InOutSummary.SoftObjectPathCount) ||
            !Reader.Read(InOutSummary.SoftObjectPathOffset))
        {
            return false;
        }
    }

    if (!Reader.ReadString(InOutSummary.LocalizationId) ||
        !Reader.Read(InOutSummary.GatherableTextDataCount) ||
        !Reader.Read(InOutSummary.GatherableTextDataOffset) ||
        !Reader.Read(InOutSummary.ExportCount) ||
        !Reader.Read(InOutSummary.ExportOffset) ||
        !Reader.Read(InOutSummary.ImportCount) ||
        !Reader.Read(InOutSummary.ImportOffset) ||
        !Reader.Read(InOutSummary.DependsOffset))
    {
        return false;
    }

    return ValidateSummary(InOutSummary, Reader.Size());
}

void CollectTableOffsetCandidates(FByteReader Reader, FUAssetPackageSummary& InOutSummary)
{
    InOutSummary.TableOffsetCandidates.clear();

    for (int32 ProbeIndex = 0; ProbeIndex < 24; ++ProbeIndex)
    {
        int32 Candidate = 0;
        if (!Reader.Read(Candidate))
        {
            break;
        }

        if (Candidate > 0 &&
            Candidate <= InOutSummary.TotalHeaderSize &&
            Candidate != InOutSummary.NameOffset &&
            Candidate != InOutSummary.ImportOffset &&
            Candidate != InOutSummary.ExportOffset &&
            Candidate != InOutSummary.SoftObjectPathOffset)
        {
            InOutSummary.TableOffsetCandidates.push_back(Candidate);
        }
    }
}

int32 FindTableEnd(size_t TableOffset, const FUAssetPackageSummary& Summary, size_t FileSize)
{
    int32 EndOffset = Summary.TotalHeaderSize > 0 ? Summary.TotalHeaderSize : static_cast<int32>(FileSize);

    const int32 Candidates[] = {
        Summary.NameOffset,
        Summary.ExportOffset,
        Summary.ImportOffset,
        Summary.DependsOffset,
        Summary.SoftObjectPathOffset,
    };

    for (int32 Candidate : Candidates)
    {
        if (Candidate > static_cast<int32>(TableOffset) && Candidate < EndOffset)
        {
            EndOffset = Candidate;
        }
    }

    for (int32 Candidate : Summary.TableOffsetCandidates)
    {
        if (Candidate > static_cast<int32>(TableOffset) && Candidate < EndOffset)
        {
            EndOffset = Candidate;
        }
    }

    if (EndOffset <= static_cast<int32>(TableOffset))
    {
        EndOffset = static_cast<int32>(FileSize);
    }

    return EndOffset;
}

bool LoadFileBytes(const MString& FilePath, TByteArray& OutData)
{
    TIfstream Input(FilePath, std::ios::binary);
    if (!Input.is_open())
    {
        return false;
    }

    Input.seekg(0, std::ios::end);
    const std::streamoff Size = Input.tellg();
    if (Size < 0)
    {
        return false;
    }

    Input.seekg(0, std::ios::beg);
    OutData.resize(static_cast<size_t>(Size));
    if (!OutData.empty())
    {
        Input.read(reinterpret_cast<char*>(OutData.data()), Size);
    }

    return Input.good() || Input.eof();
}

bool ParseSummary(FByteReader& Reader, FUAssetPackageSummary& OutSummary)
{
    if (!Reader.Read(OutSummary.Tag) || !Reader.Read(OutSummary.LegacyFileVersion))
    {
        return false;
    }

    auto ParseVersionFields = [&](FByteReader ReaderAfterLegacy, bool bHasLegacyUE3Version) -> bool
    {
        OutSummary.LegacyUE3Version = 0;
        OutSummary.FileVersionUE4 = 0;
        OutSummary.FileVersionUE5 = 0;
        OutSummary.FileVersionLicenseeUE = 0;

        if (bHasLegacyUE3Version && !ReaderAfterLegacy.Read(OutSummary.LegacyUE3Version))
        {
            return false;
        }

        if (!ReaderAfterLegacy.Read(OutSummary.FileVersionUE4))
        {
            return false;
        }

        if (OutSummary.LegacyFileVersion <= -8 && !ReaderAfterLegacy.Read(OutSummary.FileVersionUE5))
        {
            return false;
        }

        if (!ReaderAfterLegacy.Read(OutSummary.FileVersionLicenseeUE))
        {
            return false;
        }

        auto ParsePostVersionFields = [&](FByteReader ReaderAfterVersions, bool bHasSavedHashPrefix) -> bool
        {
            if (bHasSavedHashPrefix)
            {
                int32 SavedHashFormat = 0;
                if (!ReaderAfterVersions.Read(SavedHashFormat))
                {
                    return false;
                }

                if (!ReaderAfterVersions.Skip(sizeof(uint32) * 5))
                {
                    return false;
                }
            }

            if (!ReaderAfterVersions.Read(OutSummary.TotalHeaderSize) ||
                !SkipCustomVersions(ReaderAfterVersions) ||
                !ReaderAfterVersions.ReadString(OutSummary.PackageName) ||
                !ReaderAfterVersions.Read(OutSummary.PackageFlags) ||
                !ReaderAfterVersions.Read(OutSummary.NameCount) ||
                !ReaderAfterVersions.Read(OutSummary.NameOffset))
            {
                return false;
            }

            FByteReader ReaderWithSoftObjectPaths = ReaderAfterVersions;
            FUAssetPackageSummary WithSoftObjectPaths = OutSummary;
            if (TryReadSummaryLayout(ReaderWithSoftObjectPaths, true, WithSoftObjectPaths))
            {
                CollectTableOffsetCandidates(ReaderWithSoftObjectPaths, WithSoftObjectPaths);
                OutSummary = WithSoftObjectPaths;
                return true;
            }

            FByteReader ReaderWithoutSoftObjectPaths = ReaderAfterVersions;
            FUAssetPackageSummary WithoutSoftObjectPaths = OutSummary;
            if (TryReadSummaryLayout(ReaderWithoutSoftObjectPaths, false, WithoutSoftObjectPaths))
            {
                CollectTableOffsetCandidates(ReaderWithoutSoftObjectPaths, WithoutSoftObjectPaths);
                OutSummary = WithoutSoftObjectPaths;
                return true;
            }

            return false;
        };

        FByteReader ReaderWithSavedHash = ReaderAfterLegacy;
        if (ParsePostVersionFields(ReaderWithSavedHash, true))
        {
            return true;
        }

        FByteReader ReaderWithoutSavedHash = ReaderAfterLegacy;
        if (ParsePostVersionFields(ReaderWithoutSavedHash, false))
        {
            return true;
        }

        return false;
    };

    FByteReader ReaderWithLegacyUE3 = Reader;
    if (ParseVersionFields(ReaderWithLegacyUE3, true))
    {
        return true;
    }

    FByteReader ReaderWithoutLegacyUE3 = Reader;
    if (ParseVersionFields(ReaderWithoutLegacyUE3, false))
    {
        return true;
    }

    return false;
}

bool ParseNames(const TByteArray& Data, FUAssetPackage& InOutPackage)
{
    if (InOutPackage.Summary.NameCount == 0)
    {
        return true;
    }

    FByteReader Reader(Data);
    if (!Reader.Seek(static_cast<size_t>(InOutPackage.Summary.NameOffset)))
    {
        return false;
    }

    const int32 NameTableEnd = FindTableEnd(static_cast<size_t>(InOutPackage.Summary.NameOffset), InOutPackage.Summary, Data.size());
    if (NameTableEnd <= InOutPackage.Summary.NameOffset)
    {
        return false;
    }

    auto TryParseNames = [&](bool bExpectHashes) -> bool
    {
        FByteReader LocalReader = Reader;
        TVector<FUAssetNameEntry> LocalNames;
        LocalNames.reserve(static_cast<size_t>(InOutPackage.Summary.NameCount));

        for (int32 NameIndex = 0; NameIndex < InOutPackage.Summary.NameCount; ++NameIndex)
        {
            FUAssetNameEntry Entry;
            if (!LocalReader.ReadString(Entry.Name))
            {
                return false;
            }

            if (bExpectHashes)
            {
                if (!LocalReader.Read(Entry.NonCasePreservingHash) || !LocalReader.Read(Entry.CasePreservingHash))
                {
                    return false;
                }
                Entry.bHasHashes = true;
            }

            LocalNames.push_back(Entry);
        }

        if (LocalReader.Tell() > static_cast<size_t>(NameTableEnd))
        {
            return false;
        }

        InOutPackage.Names = std::move(LocalNames);
        return true;
    };

    return TryParseNames(true) || TryParseNames(false);
}

bool ParseImports(const TByteArray& Data, FUAssetPackage& InOutPackage)
{
    if (InOutPackage.Summary.ImportCount == 0)
    {
        return true;
    }

    const int32 TableEnd = FindTableEnd(static_cast<size_t>(InOutPackage.Summary.ImportOffset), InOutPackage.Summary, Data.size());
    const int32 TableSize = TableEnd - InOutPackage.Summary.ImportOffset;
    if (TableSize <= 0 || (TableSize % InOutPackage.Summary.ImportCount) != 0)
    {
        return false;
    }

    const uint32 RowSize = static_cast<uint32>(TableSize / InOutPackage.Summary.ImportCount);
    if (RowSize < 28u || RowSize > 64u)
    {
        return false;
    }

    const bool bHasPackageName = RowSize >= 36u;
    const bool bHasOptionalFlag = RowSize >= 40u;

    FByteReader Reader(Data);
    if (!Reader.Seek(static_cast<size_t>(InOutPackage.Summary.ImportOffset)))
    {
        return false;
    }

    InOutPackage.Imports.clear();
    InOutPackage.Imports.reserve(static_cast<size_t>(InOutPackage.Summary.ImportCount));

    for (int32 ImportIndex = 0; ImportIndex < InOutPackage.Summary.ImportCount; ++ImportIndex)
    {
        const size_t RowStart = Reader.Tell();

        FNameReference ClassPackage;
        FNameReference ClassName;
        FNameReference PackageName;
        FNameReference ObjectName;

        FUAssetImportEntry Entry;
        Entry.RowSize = RowSize;

        if (!ReadNameReference(Reader, ClassPackage) || !ReadNameReference(Reader, ClassName))
        {
            return false;
        }

        if (bHasPackageName && !ReadNameReference(Reader, PackageName))
        {
            return false;
        }

        if (!Reader.Read(Entry.OuterIndex) || !ReadNameReference(Reader, ObjectName))
        {
            return false;
        }

        if (bHasOptionalFlag)
        {
            uint32 RawBool = 0;
            if (!Reader.Read(RawBool))
            {
                return false;
            }
            Entry.bImportOptional = RawBool != 0;
        }

        const size_t ConsumedBytes = Reader.Tell() - RowStart;
        if (ConsumedBytes > RowSize || !Reader.Skip(RowSize - ConsumedBytes))
        {
            return false;
        }

        Entry.ClassPackageNameIndex = ClassPackage.Index;
        Entry.ClassPackageNameNumber = ClassPackage.Number;
        Entry.ClassNameIndex = ClassName.Index;
        Entry.ClassNameNumber = ClassName.Number;
        Entry.PackageNameIndex = PackageName.Index;
        Entry.PackageNameNumber = PackageName.Number;
        Entry.ObjectNameIndex = ObjectName.Index;
        Entry.ObjectNameNumber = ObjectName.Number;
        InOutPackage.Imports.push_back(Entry);
    }

    return true;
}

bool ParseExports(const TByteArray& Data, FUAssetPackage& InOutPackage)
{
    if (InOutPackage.Summary.ExportCount == 0)
    {
        return true;
    }

    const int32 TableEnd = FindTableEnd(static_cast<size_t>(InOutPackage.Summary.ExportOffset), InOutPackage.Summary, Data.size());
    const int32 TableSize = TableEnd - InOutPackage.Summary.ExportOffset;
    if (TableSize <= 0 || (TableSize % InOutPackage.Summary.ExportCount) != 0)
    {
        return false;
    }

    const uint32 RowSize = static_cast<uint32>(TableSize / InOutPackage.Summary.ExportCount);
    if (RowSize < 44u || RowSize > 256u)
    {
        return false;
    }

    FByteReader Reader(Data);
    if (!Reader.Seek(static_cast<size_t>(InOutPackage.Summary.ExportOffset)))
    {
        return false;
    }

    InOutPackage.Exports.clear();
    InOutPackage.Exports.reserve(static_cast<size_t>(InOutPackage.Summary.ExportCount));

    for (int32 ExportIndex = 0; ExportIndex < InOutPackage.Summary.ExportCount; ++ExportIndex)
    {
        const size_t RowStart = Reader.Tell();

        FNameReference ObjectName;
        FUAssetExportEntry Entry;
        Entry.RowSize = RowSize;

        if (!Reader.Read(Entry.ClassIndex) ||
            !Reader.Read(Entry.SuperIndex) ||
            !Reader.Read(Entry.TemplateIndex) ||
            !Reader.Read(Entry.OuterIndex) ||
            !ReadNameReference(Reader, ObjectName) ||
            !Reader.Read(Entry.ObjectFlags) ||
            !Reader.Read(Entry.SerialSize) ||
            !Reader.Read(Entry.SerialOffset))
        {
            return false;
        }

        const size_t ConsumedBytes = Reader.Tell() - RowStart;
        if (ConsumedBytes > RowSize || !Reader.Skip(RowSize - ConsumedBytes))
        {
            return false;
        }

        Entry.ObjectNameIndex = ObjectName.Index;
        Entry.ObjectNameNumber = ObjectName.Number;
        Entry.bSerialDataMayLiveOutsideUAsset =
            Entry.SerialOffset < 0 ||
            Entry.SerialSize < 0 ||
            static_cast<uint64>(Entry.SerialOffset) + static_cast<uint64>(Entry.SerialSize) > InOutPackage.FileSize;
        InOutPackage.Exports.push_back(Entry);
    }

    return true;
}

void ResolveNames(FUAssetPackage& InOutPackage)
{
    for (FUAssetImportEntry& ImportEntry : InOutPackage.Imports)
    {
        ImportEntry.ClassPackageName = FUAssetPackageReader::ResolveNameReference(
            InOutPackage,
            ImportEntry.ClassPackageNameIndex,
            ImportEntry.ClassPackageNameNumber);
        ImportEntry.ClassName = FUAssetPackageReader::ResolveNameReference(
            InOutPackage,
            ImportEntry.ClassNameIndex,
            ImportEntry.ClassNameNumber);
        ImportEntry.PackageName = FUAssetPackageReader::ResolveNameReference(
            InOutPackage,
            ImportEntry.PackageNameIndex,
            ImportEntry.PackageNameNumber);
        ImportEntry.ObjectName = FUAssetPackageReader::ResolveNameReference(
            InOutPackage,
            ImportEntry.ObjectNameIndex,
            ImportEntry.ObjectNameNumber);
    }

    for (FUAssetExportEntry& ExportEntry : InOutPackage.Exports)
    {
        ExportEntry.ObjectName = FUAssetPackageReader::ResolveNameReference(
            InOutPackage,
            ExportEntry.ObjectNameIndex,
            ExportEntry.ObjectNameNumber);
    }

    for (FUAssetExportEntry& ExportEntry : InOutPackage.Exports)
    {
        ExportEntry.ClassObjectName = FUAssetPackageReader::ResolvePackageObjectName(InOutPackage, ExportEntry.ClassIndex);
        ExportEntry.SuperObjectName = FUAssetPackageReader::ResolvePackageObjectName(InOutPackage, ExportEntry.SuperIndex);
        ExportEntry.OuterObjectName = FUAssetPackageReader::ResolvePackageObjectName(InOutPackage, ExportEntry.OuterIndex);
    }
}
}

bool FUAssetPackageReader::LoadFromFile(const MString& FilePath, FUAssetPackage& OutPackage, MString& OutError)
{
    OutPackage = FUAssetPackage {};
    OutPackage.FilePath = FilePath;

    if (!LoadFileBytes(FilePath, OutPackage.FileData))
    {
        OutError = "uasset_open_failed";
        return false;
    }

    OutPackage.FileSize = static_cast<uint64>(OutPackage.FileData.size());

    FByteReader Reader(OutPackage.FileData);
    if (!ParseSummary(Reader, OutPackage.Summary))
    {
        OutError = "uasset_summary_parse_failed";
        return false;
    }

    if (!ParseNames(OutPackage.FileData, OutPackage))
    {
        OutError = "uasset_name_map_parse_failed";
        return false;
    }

    if (!ParseImports(OutPackage.FileData, OutPackage))
    {
        OutError = "uasset_import_map_parse_failed";
        return false;
    }

    if (!ParseExports(OutPackage.FileData, OutPackage))
    {
        OutError = "uasset_export_map_parse_failed";
        return false;
    }

    ResolveNames(OutPackage);
    return true;
}

MString FUAssetPackageReader::ResolveNameReference(const FUAssetPackage& Package, int32 NameIndex, int32 NameNumber)
{
    if (NameIndex < 0 || static_cast<size_t>(NameIndex) >= Package.Names.size())
    {
        return {};
    }

    const MString& BaseName = Package.Names[static_cast<size_t>(NameIndex)].Name;
    if (NameNumber == 0)
    {
        return BaseName;
    }

    return BaseName + "#" + std::to_string(NameNumber);
}

MString FUAssetPackageReader::ResolvePackageObjectName(const FUAssetPackage& Package, int32 PackageIndex)
{
    if (PackageIndex == 0)
    {
        return {};
    }

    if (PackageIndex < 0)
    {
        const size_t ImportIndex = static_cast<size_t>(-PackageIndex - 1);
        if (ImportIndex >= Package.Imports.size())
        {
            return {};
        }

        return Package.Imports[ImportIndex].ObjectName;
    }

    const size_t ExportIndex = static_cast<size_t>(PackageIndex - 1);
    if (ExportIndex >= Package.Exports.size())
    {
        return {};
    }

    return Package.Exports[ExportIndex].ObjectName;
}
