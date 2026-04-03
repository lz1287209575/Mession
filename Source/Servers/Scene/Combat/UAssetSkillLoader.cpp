#include "Servers/Scene/Combat/UAssetSkillLoader.h"

#include "Common/Asset/UAssetPackageReader.h"
#include "Common/Skill/SkillNodeRegistry.h"

#include <cstdlib>
#include <cstring>
#include <cmath>

namespace
{
struct FPropertyTypeNameNode
{
    MString Name;
    TVector<FPropertyTypeNameNode> Parameters;
};

struct FTaggedPropertyView
{
    MString Name;
    MString TypeName;
    TVector<FPropertyTypeNameNode> TypeParameters;
    int32 ValueSize = 0;
    bool bIsBoolProperty = false;
    bool bBoolValue = false;
    size_t ValueOffset = 0;
};

struct FCompiledStepDefinition
{
    int32 StepIndex = 0;
    MString NodeType;
    TVector<int32> NextStepIndices;
    float FloatParam0 = 0.0f;
    float FloatParam1 = 0.0f;
    int32 IntParam0 = 0;
    MString NameParam;
    MString StringParam;
};

struct FCompiledSkillDefinition
{
    MString SkillKey;
    float CooldownSeconds = 0.0f;
    float CastTimeSeconds = 0.0f;
    float MaxRange = 0.0f;
    MString TargetType;
    TVector<FCompiledStepDefinition> Steps;
};

class FSliceReader
{
public:
    FSliceReader(const TByteArray& InData, size_t InBeginOffset, size_t InEndOffset)
        : Data(InData)
        , Offset(InBeginOffset)
        , EndOffset(InEndOffset)
    {
    }

    template<typename T>
    bool Read(T& OutValue)
    {
        static_assert(std::is_trivially_copyable_v<T>);

        if (Offset + sizeof(T) > EndOffset)
        {
            return false;
        }

        std::memcpy(&OutValue, Data.data() + Offset, sizeof(T));
        Offset += sizeof(T);
        return true;
    }

    bool ReadBytes(size_t ByteCount, size_t& OutOffset)
    {
        if (Offset + ByteCount > EndOffset)
        {
            return false;
        }

        OutOffset = Offset;
        Offset += ByteCount;
        return true;
    }

    bool ReadFString(MString& OutValue)
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
            size_t StringOffset = 0;
            if (!ReadBytes(static_cast<size_t>(SerializedLength), StringOffset))
            {
                return false;
            }

            OutValue.assign(
                reinterpret_cast<const char*>(Data.data() + StringOffset),
                reinterpret_cast<const char*>(Data.data() + StringOffset + static_cast<size_t>(SerializedLength)));
            if (!OutValue.empty() && OutValue.back() == '\0')
            {
                OutValue.pop_back();
            }
            return true;
        }

        const size_t WideCharCount = static_cast<size_t>(-SerializedLength);
        const size_t ByteCount = WideCharCount * sizeof(uint16);
        size_t StringOffset = 0;
        if (!ReadBytes(ByteCount, StringOffset))
        {
            return false;
        }

        OutValue.clear();
        OutValue.reserve(WideCharCount);
        for (size_t Index = 0; Index < WideCharCount; ++Index)
        {
            uint16 CodeUnit = 0;
            std::memcpy(&CodeUnit, Data.data() + StringOffset + (Index * sizeof(uint16)), sizeof(uint16));
            if (CodeUnit == 0)
            {
                continue;
            }

            if (CodeUnit <= 0x7Fu)
            {
                OutValue.push_back(static_cast<char>(CodeUnit));
                continue;
            }

            if (CodeUnit <= 0x7FFu)
            {
                OutValue.push_back(static_cast<char>(0xC0u | (CodeUnit >> 6)));
                OutValue.push_back(static_cast<char>(0x80u | (CodeUnit & 0x3Fu)));
                continue;
            }

            OutValue.push_back(static_cast<char>(0xE0u | (CodeUnit >> 12)));
            OutValue.push_back(static_cast<char>(0x80u | ((CodeUnit >> 6) & 0x3Fu)));
            OutValue.push_back(static_cast<char>(0x80u | (CodeUnit & 0x3Fu)));
        }

        return true;
    }

    bool Skip(size_t ByteCount)
    {
        if (Offset + ByteCount > EndOffset)
        {
            return false;
        }

        Offset += ByteCount;
        return true;
    }

    size_t GetOffset() const
    {
        return Offset;
    }

    size_t GetRemainingSize() const
    {
        return (Offset <= EndOffset) ? (EndOffset - Offset) : 0;
    }

private:
    const TByteArray& Data;
    size_t Offset = 0;
    size_t EndOffset = 0;
};

bool HasUAssetExtension(const MString& FilePath)
{
    constexpr char Extension[] = ".uasset";
    if (FilePath.size() < sizeof(Extension) - 1)
    {
        return false;
    }

    return FilePath.compare(FilePath.size() - (sizeof(Extension) - 1), sizeof(Extension) - 1, Extension) == 0;
}

bool IsValidNameReference(const FUAssetPackage& Package, int32 NameIndex, int32 NameNumber)
{
    return NameIndex >= 0 &&
        static_cast<size_t>(NameIndex) < Package.Names.size() &&
        NameNumber >= 0;
}

bool ReadResolvedName(FSliceReader& Reader, const FUAssetPackage& Package, MString& OutName)
{
    int32 NameIndex = -1;
    int32 NameNumber = 0;
    if (!Reader.Read(NameIndex) || !Reader.Read(NameNumber))
    {
        return false;
    }

    if (!IsValidNameReference(Package, NameIndex, NameNumber))
    {
        return false;
    }

    OutName = FUAssetPackageReader::ResolveNameReference(Package, NameIndex, NameNumber);
    return true;
}

bool ParseTypeNameNode(FSliceReader& Reader, const FUAssetPackage& Package, FPropertyTypeNameNode& OutNode)
{
    if (!ReadResolvedName(Reader, Package, OutNode.Name))
    {
        return false;
    }

    int32 ParameterCount = 0;
    if (!Reader.Read(ParameterCount) || ParameterCount < 0 || ParameterCount > 16)
    {
        return false;
    }

    OutNode.Parameters.clear();
    OutNode.Parameters.reserve(static_cast<size_t>(ParameterCount));
    for (int32 ParameterIndex = 0; ParameterIndex < ParameterCount; ++ParameterIndex)
    {
        FPropertyTypeNameNode Child;
        if (!ParseTypeNameNode(Reader, Package, Child))
        {
            return false;
        }
        OutNode.Parameters.push_back(std::move(Child));
    }

    return true;
}

bool ParseTaggedProperty(FSliceReader& Reader, const FUAssetPackage& Package, FTaggedPropertyView& OutProperty)
{
    OutProperty = FTaggedPropertyView {};

    if (!ReadResolvedName(Reader, Package, OutProperty.Name))
    {
        return false;
    }

    if (OutProperty.Name == "None")
    {
        return true;
    }

    if (!ReadResolvedName(Reader, Package, OutProperty.TypeName))
    {
        return false;
    }

    int32 TypeParameterCount = 0;
    if (!Reader.Read(TypeParameterCount) || TypeParameterCount < 0 || TypeParameterCount > 16)
    {
        return false;
    }

    OutProperty.TypeParameters.clear();
    OutProperty.TypeParameters.reserve(static_cast<size_t>(TypeParameterCount));
    for (int32 ParameterIndex = 0; ParameterIndex < TypeParameterCount; ++ParameterIndex)
    {
        FPropertyTypeNameNode Parameter;
        if (!ParseTypeNameNode(Reader, Package, Parameter))
        {
            return false;
        }
        OutProperty.TypeParameters.push_back(std::move(Parameter));
    }

    if (!Reader.Read(OutProperty.ValueSize) || OutProperty.ValueSize < 0)
    {
        return false;
    }

    if (OutProperty.TypeName == "BoolProperty")
    {
        uint8 BoolValue = 0;
        if (!Reader.Read(BoolValue))
        {
            return false;
        }

        OutProperty.bIsBoolProperty = true;
        OutProperty.bBoolValue = (BoolValue != 0);
        return true;
    }

    uint8 bHasPropertyGuid = 0;
    if (!Reader.Read(bHasPropertyGuid))
    {
        return false;
    }

    if (bHasPropertyGuid != 0 && !Reader.Skip(sizeof(uint32) * 4))
    {
        return false;
    }

    return Reader.ReadBytes(static_cast<size_t>(OutProperty.ValueSize), OutProperty.ValueOffset);
}

bool ReadPropertyFloat(const FTaggedPropertyView& Property, const TByteArray& Data, float& OutValue)
{
    if (Property.TypeName != "FloatProperty" || Property.ValueSize != sizeof(float))
    {
        return false;
    }

    std::memcpy(&OutValue, Data.data() + Property.ValueOffset, sizeof(float));
    return true;
}

bool ReadPropertyInt32(const FTaggedPropertyView& Property, const TByteArray& Data, int32& OutValue)
{
    if (Property.TypeName != "IntProperty" || Property.ValueSize != sizeof(int32))
    {
        return false;
    }

    std::memcpy(&OutValue, Data.data() + Property.ValueOffset, sizeof(int32));
    return true;
}

bool ReadPropertyInt32Array(const FTaggedPropertyView& Property, const FUAssetPackage& Package, TVector<int32>& OutValues)
{
    if (Property.TypeName != "ArrayProperty" || Property.TypeParameters.empty())
    {
        return false;
    }

    if (Property.TypeParameters.front().Name != "IntProperty")
    {
        return false;
    }

    FSliceReader ValueReader(
        Package.FileData,
        Property.ValueOffset,
        Property.ValueOffset + static_cast<size_t>(Property.ValueSize));

    int32 ValueCount = 0;
    if (!ValueReader.Read(ValueCount) || ValueCount < 0 || ValueCount > 256)
    {
        return false;
    }

    OutValues.clear();
    OutValues.reserve(static_cast<size_t>(ValueCount));
    for (int32 ValueIndex = 0; ValueIndex < ValueCount; ++ValueIndex)
    {
        int32 Value = 0;
        if (!ValueReader.Read(Value))
        {
            return false;
        }
        OutValues.push_back(Value);
    }

    return true;
}

bool ReadPropertyNameValue(const FTaggedPropertyView& Property, const FUAssetPackage& Package, MString& OutValue)
{
    if ((Property.TypeName != "NameProperty" && Property.TypeName != "EnumProperty") ||
        Property.ValueSize != static_cast<int32>(sizeof(int32) * 2))
    {
        return false;
    }

    FSliceReader ValueReader(Package.FileData, Property.ValueOffset, Property.ValueOffset + static_cast<size_t>(Property.ValueSize));
    return ReadResolvedName(ValueReader, Package, OutValue);
}

bool ReadPropertyStringValue(const FTaggedPropertyView& Property, const FUAssetPackage& Package, MString& OutValue)
{
    if (Property.TypeName != "StrProperty")
    {
        return false;
    }

    FSliceReader ValueReader(Package.FileData, Property.ValueOffset, Property.ValueOffset + static_cast<size_t>(Property.ValueSize));
    return ValueReader.ReadFString(OutValue);
}

bool ParseCompiledStepStruct(
    const FUAssetPackage& Package,
    size_t StructBeginOffset,
    size_t StructEndOffset,
    FCompiledStepDefinition& OutStep,
    MString& OutError)
{
    FSliceReader Reader(Package.FileData, StructBeginOffset, StructEndOffset);
    OutStep = FCompiledStepDefinition {};

    while (Reader.GetRemainingSize() > 0)
    {
        FTaggedPropertyView Property;
        if (!ParseTaggedProperty(Reader, Package, Property))
        {
            OutError = "skill_asset_step_property_parse_failed";
            return false;
        }

        if (Property.Name == "None")
        {
            return true;
        }

        if (Property.Name == "StepIndex")
        {
            (void)ReadPropertyInt32(Property, Package.FileData, OutStep.StepIndex);
            continue;
        }

        if (Property.Name == "NodeType")
        {
            (void)ReadPropertyNameValue(Property, Package, OutStep.NodeType);
            continue;
        }

        if (Property.Name == "FloatParam0")
        {
            (void)ReadPropertyFloat(Property, Package.FileData, OutStep.FloatParam0);
            continue;
        }

        if (Property.Name == "FloatParam1")
        {
            (void)ReadPropertyFloat(Property, Package.FileData, OutStep.FloatParam1);
            continue;
        }

        if (Property.Name == "IntParam0")
        {
            (void)ReadPropertyInt32(Property, Package.FileData, OutStep.IntParam0);
            continue;
        }

        if (Property.Name == "NameParam")
        {
            (void)ReadPropertyNameValue(Property, Package, OutStep.NameParam);
            continue;
        }

        if (Property.Name == "StringParam")
        {
            (void)ReadPropertyStringValue(Property, Package, OutStep.StringParam);
            continue;
        }

        if (Property.Name == "NextStepIndices")
        {
            (void)ReadPropertyInt32Array(Property, Package, OutStep.NextStepIndices);
            continue;
        }
    }

    OutError = "skill_asset_step_struct_unterminated";
    return false;
}

bool ParseCompiledStepsArray(
    const FUAssetPackage& Package,
    const FTaggedPropertyView& Property,
    TVector<FCompiledStepDefinition>& OutSteps,
    MString& OutError)
{
    if (Property.TypeName != "ArrayProperty" || Property.TypeParameters.empty())
    {
        OutError = "skill_asset_steps_property_invalid";
        return false;
    }

    const FPropertyTypeNameNode& ElementType = Property.TypeParameters.front();
    if (ElementType.Name != "StructProperty")
    {
        OutError = "skill_asset_steps_element_type_invalid";
        return false;
    }

    FSliceReader Reader(
        Package.FileData,
        Property.ValueOffset,
        Property.ValueOffset + static_cast<size_t>(Property.ValueSize));

    int32 StepCount = 0;
    if (!Reader.Read(StepCount) || StepCount < 0 || StepCount > 256)
    {
        OutError = "skill_asset_steps_count_invalid";
        return false;
    }

    OutSteps.clear();
    OutSteps.reserve(static_cast<size_t>(StepCount));
    for (int32 StepIndex = 0; StepIndex < StepCount; ++StepIndex)
    {
        FCompiledStepDefinition Step;
        if (!ParseCompiledStepStruct(Package, Reader.GetOffset(), Property.ValueOffset + static_cast<size_t>(Property.ValueSize), Step, OutError))
        {
            return false;
        }

        OutSteps.push_back(std::move(Step));

        // Advance this array reader to the end of the just-consumed element.
        FSliceReader SkipReader(Package.FileData, Reader.GetOffset(), Property.ValueOffset + static_cast<size_t>(Property.ValueSize));
        while (SkipReader.GetRemainingSize() > 0)
        {
            FTaggedPropertyView SkipProperty;
            if (!ParseTaggedProperty(SkipReader, Package, SkipProperty))
            {
                OutError = "skill_asset_steps_advance_failed";
                return false;
            }

            if (SkipProperty.Name == "None")
            {
                const size_t ConsumedBytes = SkipReader.GetOffset() - Reader.GetOffset();
                if (!Reader.Skip(ConsumedBytes))
                {
                    OutError = "skill_asset_steps_reader_advance_failed";
                    return false;
                }
                break;
            }
        }
    }

    return true;
}

bool ParseCompiledSkillStruct(
    const FUAssetPackage& Package,
    size_t StructBeginOffset,
    size_t StructEndOffset,
    FCompiledSkillDefinition& OutCompiledSkill,
    MString& OutError)
{
    FSliceReader Reader(Package.FileData, StructBeginOffset, StructEndOffset);
    OutCompiledSkill = FCompiledSkillDefinition {};

    while (Reader.GetRemainingSize() > 0)
    {
        FTaggedPropertyView Property;
        if (!ParseTaggedProperty(Reader, Package, Property))
        {
            OutError = "skill_asset_compiled_spec_property_parse_failed";
            return false;
        }

        if (Property.Name == "None")
        {
            return true;
        }

        if (Property.Name == "SkillId")
        {
            (void)ReadPropertyNameValue(Property, Package, OutCompiledSkill.SkillKey);
            continue;
        }

        if (Property.Name == "CooldownSeconds")
        {
            (void)ReadPropertyFloat(Property, Package.FileData, OutCompiledSkill.CooldownSeconds);
            continue;
        }

        if (Property.Name == "CastTimeSeconds")
        {
            (void)ReadPropertyFloat(Property, Package.FileData, OutCompiledSkill.CastTimeSeconds);
            continue;
        }

        if (Property.Name == "MaxRange")
        {
            (void)ReadPropertyFloat(Property, Package.FileData, OutCompiledSkill.MaxRange);
            continue;
        }

        if (Property.Name == "TargetType")
        {
            (void)ReadPropertyNameValue(Property, Package, OutCompiledSkill.TargetType);
            continue;
        }

        if (Property.Name == "Steps")
        {
            if (!ParseCompiledStepsArray(Package, Property, OutCompiledSkill.Steps, OutError))
            {
                return false;
            }
            continue;
        }
    }

    OutError = "skill_asset_compiled_spec_unterminated";
    return false;
}

uint32 ComputeStableSkillId(const MString& SkillKey)
{
    if (SkillKey.empty())
    {
        return 0;
    }

    bool bAllDigits = true;
    for (char Character : SkillKey)
    {
        if (Character < '0' || Character > '9')
        {
            bAllDigits = false;
            break;
        }
    }

    if (bAllDigits)
    {
        return static_cast<uint32>(std::strtoul(SkillKey.c_str(), nullptr, 10));
    }

    constexpr uint32 OffsetBasis = 2166136261u;
    constexpr uint32 Prime = 16777619u;

    uint32 Hash = OffsetBasis;
    for (char Character : SkillKey)
    {
        Hash ^= static_cast<uint8>(Character);
        Hash *= Prime;
    }

    return (Hash == 0) ? 1u : Hash;
}

ESkillTargetType ResolveTargetType(const MString& TargetTypeName, bool& bOutCanTargetSelf)
{
    bOutCanTargetSelf = false;

    if (TargetTypeName.find("Self") != MString::npos)
    {
        bOutCanTargetSelf = true;
        return ESkillTargetType::Self;
    }

    return ESkillTargetType::EnemySingle;
}

const FSkillNodeRegistryEntry* ResolveNodeRegistryEntry(const MString& NodeTypeName)
{
    const TStringView NodeTypeView(NodeTypeName.data(), NodeTypeName.size());
    if (const FSkillNodeRegistryEntry* ExactRegistryEntry = FindSkillNodeRegistryEntryByRegistryName(NodeTypeView))
    {
        return ExactRegistryEntry;
    }

    return FindSkillNodeRegistryEntryByEditorType(NodeTypeView);
}

void ApplyFloatParamSemantic(
    ESkillNodeFloatSemantic Semantic,
    float Value,
    FSkillStep& InOutStep)
{
    switch (Semantic)
    {
    case ESkillNodeFloatSemantic::RequiredRange:
        InOutStep.RequiredRange = Value;
        break;
    case ESkillNodeFloatSemantic::BaseDamage:
        InOutStep.BaseDamage = Value;
        break;
    case ESkillNodeFloatSemantic::AttackPowerScale:
        InOutStep.AttackPowerScale = Value;
        break;
    case ESkillNodeFloatSemantic::None:
    default:
        break;
    }
}

void ApplyDamageStepToSkillSpec(const TVector<FCompiledStepDefinition>& Steps, FSkillSpec& InOutSkillSpec)
{
    for (const FCompiledStepDefinition& Step : Steps)
    {
        const FSkillNodeRegistryEntry* NodeEntry = ResolveNodeRegistryEntry(Step.NodeType);
        if (!NodeEntry || NodeEntry->ServerOp != ESkillServerOp::ApplyDamage)
        {
            continue;
        }

        if (NodeEntry->FloatParam0.FloatSemantic == ESkillNodeFloatSemantic::BaseDamage)
        {
            InOutSkillSpec.BaseDamage = Step.FloatParam0;
        }

        if (NodeEntry->FloatParam1.FloatSemantic == ESkillNodeFloatSemantic::AttackPowerScale)
        {
            InOutSkillSpec.AttackPowerScale = (Step.FloatParam1 > 0.0f) ? Step.FloatParam1 : 1.0f;
        }
        return;
    }
}

void AppendCompiledStepsToSkillSpec(
    const FCompiledSkillDefinition& CompiledSkill,
    FSkillSpec& InOutSkillSpec)
{
    InOutSkillSpec.Steps.clear();
    InOutSkillSpec.Steps.reserve(CompiledSkill.Steps.size());

    for (const FCompiledStepDefinition& CompiledStep : CompiledSkill.Steps)
    {
        FSkillStep Step;
        const FSkillNodeRegistryEntry* NodeEntry = ResolveNodeRegistryEntry(CompiledStep.NodeType);
        Step.StepIndex = static_cast<uint32>(CompiledStep.StepIndex);
        Step.Op = NodeEntry ? NodeEntry->ServerOp : ESkillServerOp::End;
        if (NodeEntry && NodeEntry->bUsesSkillTargetType)
        {
            Step.TargetType = InOutSkillSpec.TargetType;
        }
        if (NodeEntry)
        {
            ApplyFloatParamSemantic(NodeEntry->FloatParam0.FloatSemantic, CompiledStep.FloatParam0, Step);
            ApplyFloatParamSemantic(NodeEntry->FloatParam1.FloatSemantic, CompiledStep.FloatParam1, Step);
        }
        Step.IntParam0 = CompiledStep.IntParam0;
        Step.NameParam = CompiledStep.NameParam;
        Step.StringParam = CompiledStep.StringParam;
        for (int32 NextStepIndex : CompiledStep.NextStepIndices)
        {
            if (NextStepIndex >= 0)
            {
                Step.NextStepIndices.push_back(static_cast<uint32>(NextStepIndex));
            }
        }
        InOutSkillSpec.Steps.push_back(std::move(Step));
    }
}

bool BuildSkillSpecFromCompiledSkill(
    const FUAssetPackage& Package,
    const FCompiledSkillDefinition& CompiledSkill,
    const FUAssetExportEntry& PrimaryExport,
    FSkillSpec& OutSkillSpec,
    MString& OutError)
{
    if (CompiledSkill.SkillKey.empty())
    {
        OutError = "skill_asset_compiled_spec_missing_skill_id";
        return false;
    }

    OutSkillSpec = FSkillSpec {};
    OutSkillSpec.SkillId = ComputeStableSkillId(CompiledSkill.SkillKey);
    OutSkillSpec.SkillName = CompiledSkill.SkillKey.empty() ? PrimaryExport.ObjectName : CompiledSkill.SkillKey;
    OutSkillSpec.CastRange = CompiledSkill.MaxRange;
    OutSkillSpec.CooldownMs = static_cast<uint32>(std::lround(std::max(0.0f, CompiledSkill.CooldownSeconds) * 1000.0f));
    OutSkillSpec.CastTimeSeconds = CompiledSkill.CastTimeSeconds;
    OutSkillSpec.TargetType = ResolveTargetType(CompiledSkill.TargetType, OutSkillSpec.bCanTargetSelf);
    ApplyDamageStepToSkillSpec(CompiledSkill.Steps, OutSkillSpec);
    AppendCompiledStepsToSkillSpec(CompiledSkill, OutSkillSpec);

    if (OutSkillSpec.BaseDamage <= 0.0f)
    {
        OutSkillSpec.BaseDamage = 1.0f;
    }

    if (OutSkillSpec.AttackPowerScale <= 0.0f)
    {
        OutSkillSpec.AttackPowerScale = 1.0f;
    }

    if (OutSkillSpec.Steps.empty())
    {
        OutSkillSpec.Steps = {
            FSkillStep {.StepIndex = 0, .Op = ESkillServerOp::Start},
            FSkillStep {.StepIndex = 1, .Op = ESkillServerOp::SelectTarget, .TargetType = OutSkillSpec.TargetType},
            FSkillStep {
                .StepIndex = 2,
                .Op = ESkillServerOp::ApplyDamage,
                .TargetType = OutSkillSpec.TargetType,
                .BaseDamage = OutSkillSpec.BaseDamage,
                .AttackPowerScale = OutSkillSpec.AttackPowerScale},
            FSkillStep {.StepIndex = 3, .Op = ESkillServerOp::End},
        };
    }

    (void)Package;
    return true;
}

bool ParseNCSkillGraphAsset(
    const FUAssetPackage& Package,
    const FUAssetExportEntry& PrimaryExport,
    FSkillSpec& OutSkillSpec,
    MString& OutError)
{
    const uint64 ValueEnd = static_cast<uint64>(PrimaryExport.SerialOffset) + static_cast<uint64>(PrimaryExport.SerialSize);
    if (PrimaryExport.SerialOffset < 0 ||
        PrimaryExport.SerialSize <= 0 ||
        ValueEnd > Package.FileData.size())
    {
        OutError = "skill_asset_export_payload_invalid";
        return false;
    }

    FSliceReader Reader(
        Package.FileData,
        static_cast<size_t>(PrimaryExport.SerialOffset),
        static_cast<size_t>(ValueEnd));

    if (Reader.GetRemainingSize() > 9)
    {
        const size_t ProbeOffset = Reader.GetOffset() + 1;
        if (Package.FileData[Reader.GetOffset()] == 0 &&
            ProbeOffset + (sizeof(int32) * 2) <= ValueEnd)
        {
            int32 ProbeNameIndex = -1;
            int32 ProbeNameNumber = -1;
            std::memcpy(&ProbeNameIndex, Package.FileData.data() + ProbeOffset, sizeof(int32));
            std::memcpy(&ProbeNameNumber, Package.FileData.data() + ProbeOffset + sizeof(int32), sizeof(int32));
            if (IsValidNameReference(Package, ProbeNameIndex, ProbeNameNumber))
            {
                (void)Reader.Skip(1);
            }
        }
    }

    FCompiledSkillDefinition CompiledSkill;
    bool bFoundCompiledSpec = false;

    while (Reader.GetRemainingSize() > 0)
    {
        FTaggedPropertyView Property;
        if (!ParseTaggedProperty(Reader, Package, Property))
        {
            OutError = "skill_asset_root_property_parse_failed";
            return false;
        }

        if (Property.Name == "None")
        {
            break;
        }

        if (Property.Name != "CompiledSpec" || Property.TypeName != "StructProperty")
        {
            continue;
        }

        if (!ParseCompiledSkillStruct(
                Package,
                Property.ValueOffset,
                Property.ValueOffset + static_cast<size_t>(Property.ValueSize),
                CompiledSkill,
                OutError))
        {
            return false;
        }

        bFoundCompiledSpec = true;
        break;
    }

    if (!bFoundCompiledSpec)
    {
        OutError = "skill_asset_compiled_spec_missing";
        return false;
    }

    return BuildSkillSpecFromCompiledSkill(Package, CompiledSkill, PrimaryExport, OutSkillSpec, OutError);
}

MString BuildPackageDiagnostic(const FUAssetPackage& Package)
{
    MString Diagnostic =
        "package=" + Package.Summary.PackageName +
        ",names=" + std::to_string(Package.Names.size()) +
        ",imports=" + std::to_string(Package.Imports.size()) +
        ",exports=" + std::to_string(Package.Exports.size());

    if (!Package.Exports.empty())
    {
        const FUAssetExportEntry& PrimaryExport = Package.Exports.front();
        Diagnostic += ",primary_export=" + PrimaryExport.ObjectName;
        if (!PrimaryExport.ClassObjectName.empty())
        {
            Diagnostic += ",primary_class=" + PrimaryExport.ClassObjectName;
        }
        if (PrimaryExport.bSerialDataMayLiveOutsideUAsset)
        {
            Diagnostic += ",primary_payload=external_or_split";
        }
    }

    return Diagnostic;
}
}

bool FUAssetSkillLoader::LoadSkillSpecFromFile(
    const MString& FilePath,
    FSkillSpec& OutSkillSpec,
    MString& OutError)
{
    OutSkillSpec = FSkillSpec {};

    if (!HasUAssetExtension(FilePath))
    {
        OutError = "skill_asset_extension_invalid";
        return false;
    }

    FUAssetPackage Package;
    if (!FUAssetPackageReader::LoadFromFile(FilePath, Package, OutError))
    {
        OutError = "skill_asset_unpack_failed:" + OutError;
        return false;
    }

    if (Package.Exports.empty())
    {
        OutError = "skill_asset_export_missing:" + BuildPackageDiagnostic(Package);
        return false;
    }

    const FUAssetExportEntry& PrimaryExport = Package.Exports.front();
    if (!ParseNCSkillGraphAsset(Package, PrimaryExport, OutSkillSpec, OutError))
    {
        OutError = OutError + ":" + BuildPackageDiagnostic(Package);
        return false;
    }

    return true;
}
