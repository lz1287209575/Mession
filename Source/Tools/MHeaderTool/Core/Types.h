#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <set>
#include <optional>
#include <filesystem>
#include <cstdint>

namespace fs = std::filesystem;

namespace MHeaderTool
{

// ============================================================================
// Enums
// ============================================================================

enum class EParsedTypeKind : uint8_t
{
    Class,
    Struct,
    Enum
};

// ============================================================================
// Validation
// ============================================================================

struct SValidationRule
{
    std::string FieldName;
    std::string ValidatorName;
};

// ============================================================================
// Parsed Types
// ============================================================================

struct SParsedParameter
{
    std::string Type;
    std::string StorageType;
    std::string Name;
    std::string PropertyKind;
};

struct SMetadataEntry
{
    std::string Key;
    std::string Value;
};

struct SParsedProperty
{
    std::string MacroArgs;
    std::string Type;
    std::string Name;
    std::string PropertyKind;
    std::string FlagsExpr;
    std::string Owner;
    std::vector<SMetadataEntry> Metadata;
};

struct SParsedFunction
{
    std::string MacroArgs;
    std::string ReturnType;
    std::string ReturnStorageType;
    std::string ReturnPropertyKind;
    std::string Name;
    std::string Signature;
    std::string Owner;
    bool bConst = false;
    bool bHasValidate = false;
    bool bIsAsync = false;
    bool bIsRpc = false;
    bool bIsPlayerRpc = false;
    bool bReliable = true;
    std::string AsyncBody;
    std::string Transport;
    std::string RpcKind;
    std::string Endpoint;
    std::string MessageName;
    std::string Route;
    std::string Target;
    std::string Auth;
    std::string Wrap;
    std::string ClientApi;
    std::vector<SParsedParameter> Params;
    std::vector<SValidationRule> ValidationRules;
    std::vector<std::string> DependencyList;
};

struct SParsedClass
{
    EParsedTypeKind Kind = EParsedTypeKind::Class;
    std::string Name;
    fs::path HeaderPath;
    size_t SourceLine = 0;  // 行号，用于去重
    std::string ParentClass = "MObject";
    std::string ClassFlagsExpr = "0";
    std::string ReflectionType = "Object";
    std::string Owner;
    std::string InjectionClass;
    std::vector<SParsedFunction> InjectionFunctions;
    std::vector<SParsedProperty> InjectionProperties;
    bool bScopedEnum = false;
    std::string EnumUnderlyingType = "int32";
    std::map<std::string, std::string> TypeAliases;
    std::vector<SParsedProperty> Properties;
    std::vector<SParsedFunction> Functions;
    std::vector<std::string> EnumValues;
};

// ============================================================================
// Validation Schema
// ============================================================================

struct SValidationSchemaField
{
    std::string Name;
    std::string Kind;
    std::optional<std::string> TypeName;
    std::optional<std::string> ItemKind;
    std::optional<std::string> ItemTypeName;
    std::optional<size_t> Size;
};

// ============================================================================
// Configuration
// ============================================================================

struct SOptions
{
    fs::path SourceRoot = "Source";
    fs::path OutputDir = "Build/Generated";
    fs::path CacheDir = "Build/.mheadertool_cache";
    fs::path CMakeManifestPath = "Build/Generated/MHeaderToolTargets.cmake";
    fs::path ValidationSchemaPath = "Build/Generated/ValidationProtocolSchema.json";
    bool bVerbose = false;
    bool bIncremental = true;
    bool bForceFull = false;
    bool bBenchmark = false;
    bool bDryRun = false;
    int NumThreads = 0;  // 0 = auto (CPU cores)
};

// ============================================================================
// Type Aliases
// ============================================================================

using TRpcListMacroMap = std::map<std::string, std::vector<SParsedFunction>>;

}  // namespace MHeaderTool
