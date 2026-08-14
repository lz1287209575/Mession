// ============================================================================
// MCodeGenerator - IR-based generator entry points (A2 / Task 7)
//
// Mirrors the legacy `GenerateHeader` / `GenerateSource` /
// `EmitAsyncFramesHeader` methods on `MCodeGenerator`, but consumes
// `SParsedRecord` from the AST path. The legacy implementations remain
// inline in `Generation/CodeGenerator.h`; A3 cleanup will delete them.
//
// Strategy (A2 / Task 7 simplest implementation):
//   * Build a legacy `SParsedClass` shim from the `SParsedRecord` IR node,
//     then delegate to the existing inline `Generate*Header` /
//     `Generate*Source` / `EmitAsyncFramesHeader` helpers. This guarantees
//     byte-equal parity with the legacy path (Task 9 `A2DiffTest` will
//     verify).
//   * The shim only fills the fields the existing helpers actually read
//     (`Name`, `HeaderPath`, `Kind`, `ReflectionType`, `Owner`, `Properties`,
//     `Functions`, `AllParentClasses`, `EnumValues`, `bScopedEnum`).
// ============================================================================

#include "Generation/CodeGenerator.h"

#include "Common/Runtime/MLib.h"

#include <sstream>
#include <string>
#include <vector>

namespace MHeaderTool
{

namespace
{

// Local alias — `MStringStream` is the brief's verbatim type for the
// generator. Project's MLib only exposes `MString`, but Task 7 brief uses
// `MStringStream` in the canonical snippet. Defining it here keeps the
// .cpp self-contained without polluting the global MLib header.
using MStringStream = std::ostringstream;

// 对齐旧 Parsing/ClassParser.h::DetermineOwnerFromHeaderPath：
// 路径里找 `Source/Servers/<Service>/` → <Service>，否则 "Shared"。
// ManifestGenerators 按 Owner 分 CMake generated group（echoservice /
// gateway / serviceregistry / shared），Owner 错误会让所有类型挤进
// shared 组，链接任一服务时引用其它服务的业务符号（vtable / ServerCall
// 实现）导致 undefined reference。
MString DetermineOwnerFromHeaderPath(const fs::path& HeaderPath)
{
    const MString P = HeaderPath.generic_string();
    const MString Needle = "/Source/Servers/";
    const size_t Pos = P.find(Needle);
    if (Pos != MString::npos)
    {
        const size_t NameStart = Pos + Needle.size();
        const size_t NameEnd = P.find('/', NameStart);
        if (NameEnd != MString::npos)
        {
            return P.substr(NameStart, NameEnd - NameStart);
        }
    }
    return "Shared";
}

// 对齐旧 CodeGenerator.h::InferPropertyKind（legacy 生成器用它给
// CreateOffsetProperty 填 EPropertyType::<Kind>）。
MString InferPropertyKindCompat(const MString& TypeName)
{
    MString Compact = TypeName;
    Compact.erase(std::remove_if(Compact.begin(), Compact.end(),
        [](char C) { return std::isspace(static_cast<unsigned char>(C)); }),
        Compact.end());
    if (Compact == "int8") return "Int8";
    if (Compact == "int16") return "Int16";
    if (Compact == "int32") return "Int32";
    if (Compact == "int64") return "Int64";
    if (Compact == "uint8") return "UInt8";
    if (Compact == "uint16") return "UInt16";
    if (Compact == "uint32") return "UInt32";
    if (Compact == "uint64") return "UInt64";
    if (Compact == "float") return "Float";
    if (Compact == "double") return "Double";
    if (Compact == "bool") return "Bool";
    if (Compact == "MString") return "String";
    if (Compact == "MName") return "Name";
    if (Compact == "SVector") return "Vector";
    if (Compact == "SRotator") return "Rotator";
    if (Compact.rfind("TVector<", 0) == 0 || Compact.rfind("TByteArray<", 0) == 0
        || Compact.rfind("TMap<", 0) == 0 || Compact.rfind("TSet<", 0) == 0)
    {
        return "Array";
    }
    return "Struct";
}

// 对齐旧 Parsing/FunctionParser.h 的 NormalizeReflectionType：
// 去首尾空白、`const ` 前缀、尾随 `&`/`*`。legacy 生成器用 StorageType
// （规范化名）做 include / 类型名，用 Type（源码拼写）做参数声明。
MString NormalizeReflectionType(MString TypeName)
{
    auto Trim = [](MString In)
    {
        const size_t B = In.find_first_not_of(" \t\r\n");
        if (B == MString::npos) return MString();
        const size_t E = In.find_last_not_of(" \t\r\n");
        return In.substr(B, E - B + 1);
    };
    TypeName = Trim(TypeName);
    while (TypeName.rfind("const ", 0) == 0)
    {
        TypeName = Trim(TypeName.substr(6));
    }
    while (!TypeName.empty() && (TypeName.back() == '&' || TypeName.back() == '*'))
    {
        TypeName.pop_back();
        TypeName = Trim(TypeName);
    }
    return TypeName;
}

// ---------------------------------------------------------------------------
// IR → legacy `SParsedClass` shim
// ---------------------------------------------------------------------------
//
// Task 7 brief: simplest implementation. Map the IR record fields the
// existing inline helpers actually read into a legacy `SParsedClass` and
// call those helpers verbatim. Any IR-only fields (`bHasAsyncFunctions`,
// `AwaitSites`, `LiveAcrossAwait`, ...) are unused by the legacy codegen
// helpers — Task 9 `A2DiffTest` proves byte-equal parity.
SParsedProperty ToLegacyProperty(const mession::headercodegen::SParsedProperty& In)
{
    SParsedProperty Out;
    Out.Name         = In.Name;
    Out.Type         = In.Type.CanonicalName;
    Out.FlagsExpr    = In.FlagsExpr;
    // Mirror FlagsExpr to MacroArgs so legacy BuildPropertyFlagsExpr
    // (which scans the MacroArgs token stream, not FlagsExpr) gets the
    // actual MPROPERTY(...) args instead of an empty string. Without
    // this, every flagged MPROPERTY(PersistentData|Replicated) emits
    // EPropertyFlags::None in the registered property.
    Out.MacroArgs    = In.FlagsExpr;
    Out.Owner        = In.Owner;
    for (const auto& M : In.Metadata)
    {
        Out.Metadata.push_back({M.Key, M.Value});
    }
    return Out;
}

SParsedFunction ToLegacyFunction(const mession::headercodegen::SParsedFunction& In)
{
    SParsedFunction Out;
    Out.Name               = In.Name;
    Out.bConst             = In.bConst;
    Out.bIsAsync           = In.bIsAsync;
    Out.bIsRpc             = In.bIsRpc;
    Out.bIsPlayerRpc       = In.bIsPlayerRpc;
    Out.bReliable          = In.bReliable;
    Out.AsyncBody          = In.AsyncBody;
    Out.MessageName        = In.MessageName;
    Out.Route              = In.Route;
    Out.Target             = In.Target;
    Out.Auth               = In.Auth;
    Out.Wrap               = In.Wrap;
    Out.ClientApi          = In.ClientApi;
    Out.Endpoint           = In.Endpoint;
    Out.DependencyList     = In.DependencyList;

    // IR uses strongly-typed enums; legacy codegen expects lowercase
    // string identifiers. Map back to the same vocabulary the string
    // parser produces.
    switch (In.Transport)
    {
        case mession::headercodegen::EFunctionTransport::ServerCall:
            Out.Transport = "ServerCall"; break;
        case mession::headercodegen::EFunctionTransport::ClientCall:
            Out.Transport = "ClientCall"; break;
        case mession::headercodegen::EFunctionTransport::Client:
            Out.Transport = "Client"; break;
        case mession::headercodegen::EFunctionTransport::LuaBind:
            Out.Transport = "LuaBind"; break;
        case mession::headercodegen::EFunctionTransport::None:
        default:
            Out.Transport = ""; break;
    }
    switch (In.RpcKind)
    {
        case mession::headercodegen::ERpcKind::Server:
            Out.RpcKind = "Server"; break;
        case mession::headercodegen::ERpcKind::Client:
            Out.RpcKind = "Client"; break;
        case mession::headercodegen::ERpcKind::None:
        default:
            Out.RpcKind = ""; break;
    }

    // Return type — IR 存源码拼写（QualTypeToSParsedType + TypeSourceInfo）；
    // legacy 期望 Type = 源码拼写、ReturnStorageType = 规范化名（去 const/&）。
    Out.ReturnType        = In.ReturnType.CanonicalName;
    Out.ReturnStorageType = NormalizeReflectionType(In.ReturnType.CanonicalName);

    // Parameters — SParsedParameter → SParsedParameter (legacy)。
    for (const auto& P : In.Params)
    {
        SParsedParameter LP;
        LP.Name         = P.Name;
        LP.Type         = P.Type.CanonicalName;
        LP.StorageType  = NormalizeReflectionType(P.Type.CanonicalName);
        // legacy 生成器 CreateOffsetProperty 直接拼 EPropertyType::<PropertyKind>
        LP.PropertyKind = InferPropertyKindCompat(LP.StorageType);
        Out.Params.push_back(std::move(LP));
    }

    return Out;
}

SParsedClass ToLegacyClass(const mession::headercodegen::SParsedRecord& In)
{
    SParsedClass Out;
    Out.Kind              = (In.Kind == mession::headercodegen::ERecordKind::Struct)
                                ? EParsedTypeKind::Struct
                                : EParsedTypeKind::Class;
    Out.Name              = In.Name;
    Out.HeaderPath        = In.HeaderPath;
    Out.ParentClass       = In.ParentClass.empty() ? "MObject" : In.ParentClass;
    Out.ClassFlagsExpr    = In.ClassFlagsExpr.empty() ? "0" : In.ClassFlagsExpr;
    Out.ReflectionType    = In.ReflectionType.empty() ? "Object" : In.ReflectionType;
    Out.Owner             = In.Owner;
    Out.AllParentClasses  = In.AllParentClasses;
    Out.TypeAliases       = In.TypeAliases;

    for (const auto& P : In.Properties)
    {
        Out.Properties.push_back(ToLegacyProperty(P));
    }
    for (const auto& F : In.Functions)
    {
        Out.Functions.push_back(ToLegacyFunction(F));
    }
    // manifest 分组键（旧字符串解析路径有同名推导）
    if (Out.Owner.empty())
    {
        Out.Owner = DetermineOwnerFromHeaderPath(Out.HeaderPath);
    }
    return Out;
}

}  // namespace

// ============================================================================
// GenerateHeaderFromIR
// ============================================================================
//
// Brief verbatim (with internal helper renames so the snippet matches the
// existing inline `GenerateStructHeader` / `GenerateClassHeader` helpers):
//   * GenerateHeaderFromIR(Record) writes the boilerplate (#pragma once,
//     generated banner, source path, common includes) and then dispatches
//     to the legacy struct/class codegen via the IR → legacy shim.
//   * The legacy helpers require `ostringstream`; we use `MStringStream`
//     alias per the brief's verbatim type.

MString MCodeGenerator::GenerateHeaderFromIR(
    const SParsedRecord& Record,
    const TVector<SParsedRecord>& /*AllRecordsInSameHeader*/) const
{
    MStringStream Out;
    Out << "#pragma once\n";
    Out << "// Generated by MHeaderTool\n";
    Out << "// Source: " << Record.HeaderPath.string() << "\n";
    Out << "// Reflected "
        << (Record.Kind == mession::headercodegen::ERecordKind::Struct ? "struct" : "class")
        << ": " << Record.Name << "\n";
    Out << "\n";

    Out << "#include \"Common/Runtime/Reflect/Reflection.h\"\n";
    Out << "#include \"Common/Runtime/Async/MAsync.h\"\n";
    Out << "#include \"Common/Net/Rpc/RpcClientCall.h\"\n";
    // 与 legacy GenerateHeader 一致：include 用户头前不额外空行
    Out << "#include \"" << MakeIncludePathFromHeader(Record.HeaderPath) << "\"\n";
    Out << "\n";

    SParsedClass Legacy = ToLegacyClass(Record);
    if (Legacy.Kind == EParsedTypeKind::Struct)
    {
        GenerateStructHeader(Out, Legacy);
    }
    else
    {
        GenerateClassHeader(Out, Legacy);
    }
    return Out.str();
}

// ============================================================================
// GenerateSourceFromIR
// ============================================================================
//
// Same shape as legacy `GenerateSource`: emit a generated banner + the
// matching `.mgenerated.h` + user header includes, then dispatch to the
// legacy enum/struct/class source codegen via the IR → legacy shim.

MString MCodeGenerator::GenerateSourceFromIR(const SParsedRecord& Record) const
{
    MStringStream Out;
    const MString IncludeName    = MakeIncludePathFromHeader(Record.HeaderPath);
    const MString GeneratedHeader = SanitizeIdentifier(Record.Name) + ".mgenerated.h";

    Out << "// Generated by MHeaderTool\n";
    Out << "// Source: " << Record.HeaderPath.string() << "\n";
    Out << "#include \"" << GeneratedHeader << "\"\n";
    Out << "#include \"" << IncludeName << "\"\n";
    Out << "\n\n";

    SParsedClass Legacy = ToLegacyClass(Record);
    if (Legacy.Kind == EParsedTypeKind::Struct)
    {
        GenerateStructSource(Out, Legacy);
    }
    else
    {
        GenerateClassSource(Out, Legacy);
    }
    return Out.str();
}

// ============================================================================
// EmitAsyncFramesHeaderFromIR
// ============================================================================
//
// Mirrors legacy `EmitAsyncFramesHeader`: skip if no async functions,
// then emit the dedicated `<ClassName>_AsyncFrames.h` contents. Delegates
// to the existing `GenerateAsyncStateMachine` helper via the legacy shim.

MString MCodeGenerator::EmitAsyncFramesHeaderFromIR(const SParsedRecord& Record) const
{
    // AsyncFrames only apply to classes (not enums or structs).
    if (Record.Kind != mession::headercodegen::ERecordKind::Class)
    {
        return {};
    }

    bool bHasAsync = false;
    for (const auto& func : Record.Functions)
    {
        if (func.bIsAsync) { bHasAsync = true; break; }
    }
    if (!bHasAsync) return {};

    MStringStream Out;
    Out << "#pragma once\n";
    Out << "// Generated by MHeaderTool\n";
    Out << "// Source: " << Record.HeaderPath.string() << "\n";
    Out << "// Async Frame struct definitions for " << Record.Name << "\n";
    Out << "// (P3 v1 inline-body design — see spec 2026-07-24 §7.3)\n";
    Out << "//\n";
    Out << "// The user header must #include this file BEFORE `class "
        << Record.Name << "` so inline async bodies can construct\n";
    Out << "// and use the Frame via AWAIT_OK.\n";
    Out << "\n";

    Out << "#include \"Common/Runtime/Async/MAsync.h\"\n";
    Out << "#include \"Common/Runtime/Reflect/Reflection.h\"\n";
    Out << "\n";

    SParsedClass Legacy = ToLegacyClass(Record);
    for (const auto& func : Legacy.Functions)
    {
        if (func.bIsAsync)
        {
            GenerateAsyncStateMachine(Out, Legacy, func);
        }
    }

    return Out.str();
}

SParsedClass ToLegacyEnum(const mession::headercodegen::SParsedEnum& In);  // 定义在下方

// ---------------------------------------------------------------------------
// ToLegacyClasses — 整树 IR → legacy SParsedClass 集合
// ---------------------------------------------------------------------------
//
// A2 follow-up (AST 重构 merge)。ManifestGenerators / LuaBindEmitter 仍消费
// legacy `SParsedClass`（Core/Types.h）；AST 路径产出 `SParseIR`。这里
// 复用 anonymous namespace 里的 ToLegacyClass（同一 TU 可见），逐 Record
// 转换，与 GenerateHeaderFromIR 内部的单 Record 转换完全一致。
TVector<SParsedClass> MCodeGenerator::ToLegacyClasses(
    const mession::headercodegen::SParseIR& IR) const
{
    // 只含 Records（类/结构）。enum 不进 manifest/构建组：
    //  - legacy 的 enum 生成本就是 no-op 注册 stub（namespace 级 scoped enum
    //    无法在全局作用域引用），编译与否无功能差异；
    //  - AST 版会扫描到所有 Source/ 内 scoped enum（含用户内部实现 enum，
    //    如 MLuaVector.h 的 MScalarType）——若进 shared 组编译，其 .mgenerated.cpp
    //    include 用户头（可能带 <lua.h> 等外部依赖）会破坏构建。
    TVector<SParsedClass> Out;
    Out.reserve(IR.Records.size());
    for (const auto& Record : IR.Records)
    {
        Out.push_back(ToLegacyClass(Record));
    }
    return Out;
}

// ---------------------------------------------------------------------------
// ToLegacyEnum — SParsedEnum → legacy SParsedClass(Enum) shim
// ---------------------------------------------------------------------------
//
// legacy GenerateHeader/GenerateSource 通过 SParsedClass.Kind == Enum 分发
// 到 GenerateEnumHeader/GenerateEnumSource。AST 的 SParseIR::Enums 只填了
// Name / HeaderPath / bScopedEnum / Values；Owner 恒为空（namespace 级
// enum），对应 legacy 的 no-op 注册 stub 行为。
SParsedClass ToLegacyEnum(const mession::headercodegen::SParsedEnum& In)
{
    SParsedClass Out;
    Out.Name           = In.Name;
    Out.HeaderPath     = In.HeaderPath;
    Out.Kind           = EParsedTypeKind::Enum;
    Out.bScopedEnum    = In.bScopedEnum;
    Out.ReflectionType = "Enum";
    Out.EnumValues     = In.Values;
    // Owner 保持空：对齐旧 EnumParser（不设 Owner）——legacy
    // GenerateEnumHeader 对 `bScopedEnum && Owner.empty()` 生成 no-op 注册
    // stub（namespace 级 scoped enum 在全局作用域无法引用，如 mession::script::EReloadMode）。
    return Out;
}

MString MCodeGenerator::GenerateEnumHeaderFromIR(
    const mession::headercodegen::SParsedEnum& InEnum) const
{
    return GenerateHeader(ToLegacyEnum(InEnum), {});
}

MString MCodeGenerator::GenerateEnumSourceFromIR(
    const mession::headercodegen::SParsedEnum& InEnum) const
{
    return GenerateSource(ToLegacyEnum(InEnum));
}

}  // namespace MHeaderTool