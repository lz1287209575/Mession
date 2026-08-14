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
    MString FieldName;
    MString ValidatorName;
};

// ============================================================================
// Parsed Types
// ============================================================================

struct SParsedParameter
{
    MString Type;
    MString StorageType;
    MString Name;
    MString PropertyKind;
};

struct SMetadataEntry
{
    MString Key;
    MString Value;
};

struct SParsedProperty
{
    MString MacroArgs;
    MString Type;
    MString Name;
    MString PropertyKind;
    MString FlagsExpr;
    MString Owner;
    TVector<SMetadataEntry> Metadata;
};

struct SParsedFunction
{
    MString MacroArgs;
    MString ReturnType;
    MString ReturnStorageType;
    MString ReturnPropertyKind;
    MString Name;
    MString Signature;
    MString Owner;
    bool bConst = false;
    bool bHasValidate = false;
    bool bIsAsync = false;
    bool bIsRpc = false;
    bool bIsPlayerRpc = false;
    bool bReliable = true;
    MString AsyncBody;
    MString Transport;
    MString RpcKind;
    MString Endpoint;
    MString MessageName;
    MString Route;
    MString Target;
    MString Auth;
    MString Wrap;
    MString ClientApi;
    TVector<SParsedParameter> Params;
    TVector<SValidationRule> ValidationRules;
    TVector<MString> DependencyList;
};

struct SParsedClass
{
    EParsedTypeKind Kind = EParsedTypeKind::Class;
    MString Name;
    fs::path HeaderPath;
    size_t SourceLine = 0;  // 行号，用于去重
    MString ParentClass = "MObject";
    MString ClassFlagsExpr = "0";
    MString ReflectionType = "Object";
    MString Owner;
    TVector<MString> AllParentClasses;  // 所有基类，用于检查是否继承自 MServerCallProxyBase
    bool bScopedEnum = false;
    MString EnumUnderlyingType = "int32";
    TMap<MString, MString> TypeAliases;
    TVector<SParsedProperty> Properties;
    TVector<SParsedFunction> Functions;
    TVector<MString> EnumValues;
};

// ============================================================================
// Free Async Function (P4 — spec 2026-07-28 §B)
// ============================================================================
//
// Captures a namespace-scope free function declared with `MFUNCTION(Async)`.
// Unlike `SParsedFunction` (which lives inside a class), this struct has no
// owning class — only the header path + function identity. Consumed by
// `MCodeGenerator::EmitFreeAsyncFramesHeader` to emit one Frame struct per
// free async function into `<Header>_FreeAsyncFrames.mgenerated.h`.

struct SFreeAsyncFunc
{
    fs::path HeaderPath;
    MString Name;
    MString ResponseType;
    MString AsyncBody;
};

// ============================================================================
// Validation Schema
// ============================================================================

struct SValidationSchemaField
{
    MString Name;
    MString Kind;
    TOptional<MString> TypeName;
    TOptional<MString> ItemKind;
    TOptional<MString> ItemTypeName;
    TOptional<size_t> Size;
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
    fs::path ClientManifestPath = "Build/Generated/MClientManifest.mgenerated.cpp";
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

using TRpcListMacroMap = TMap<MString, TVector<SParsedFunction>>;

}  // namespace MHeaderTool
