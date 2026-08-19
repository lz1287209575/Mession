#pragma once

#include "Common/Runtime/MLib.h"
#include <cstdint>
#include <filesystem>

namespace fs = std::filesystem;

namespace mession::headercodegen {

    // spec §2.3: 记录类型分类
    enum class ERecordKind : uint8_t { Class, Struct };

    // spec §2.4: 函数传输语义分类（MFUNCTION 上的 ServerCall/ClientCall/Client 等）
    enum class EFunctionTransport : uint8_t { None, ServerCall, ClientCall, Client, LuaBind, CallClient };

    // spec §2.5: RPC 调用方向（Server↔Client 哪一侧发起）
    enum class ERpcKind : uint8_t { None, Server, Client };

    // spec §2.6: enum 普通 vs scoped（enum class）
    enum class EEnumKind : uint8_t { Plain, Scoped };

    // spec §2.7: 异步等待点种类（AWAIT_OK / TAwaitable<> / 裸 Awaitable）
    enum class EAwaitSiteKind : uint8_t { AwaitOkMacro, TAwaitableCall, AwaitableBare };

    // spec §2.3: 解析后的类型，含模板参数与 cvr 标记
    struct SParsedType {
        MString              CanonicalName;
        bool                 bReference = false;
        bool                 bConst     = false;
        bool                 bPointer   = false;
        TVector<SParsedType> TemplateArgs;
        MString              ResolvedClassName;
    };

    // spec §2.3: 函数参数（名称 + 类型）
    struct SParsedParameter {
        MString     Name;
        SParsedType Type;
    };

    // spec §2.3: MProperty 反射字段
    // 单条 MPROPERTY Meta=(K=V,...) 元数据（字段与 Core/Types.h 的
    // SMetadataEntry 对齐；ToLegacyProperty 逐字段拷贝）。
    struct SMetadataEntry {
        MString Key;
        MString Value;
    };

    struct SParsedProperty {
        MString     Name;
        SParsedType Type;
        MString     Owner;
        MString     FlagsExpr;

        TVector<SMetadataEntry> Metadata;
    };

    // spec §2.3: enum 单值（名字 + 显式取值）
    struct SParsedEnumValue {
        MString Name;
        int64   Value = 0;
    };

    // spec §2.3: enum 整体（普通 vs scoped + 基础类型 + 值列表）
    struct SParsedEnum {
        EEnumKind                 EnumKind = EEnumKind::Plain;
        MString                   Name;
        fs::path                  HeaderPath;
        MString                   UnderlyingType;
        TVector<MString>          Values;
        TVector<SParsedEnumValue> ValuesDetailed;
        bool                      bScopedEnum = false;
    };

    // spec §2.3: 类型别名（using A = B;）
    struct SParsedTypeAlias {
        MString  Name;
        MString  UnderlyingType;
        fs::path HeaderPath;
    };

    // spec §2.3: 反射函数（RPC / client surface / async / 普通）
    // 注意 SParsedFunction 持有 SParsedRecord::Functions 与 SParseIR::FreeFunctions；
    // 而 SParsedRecord 本身在字段中引用了 SParsedFunction——此处先前置声明。
    struct SParsedFunction;

    // spec §2.3: 单个 await 站点（AWAIT_OK / TAwaitable<...> / 裸 Awaitable）
    struct SAwaitSite {
        EAwaitSiteKind Kind       = EAwaitSiteKind::AwaitOkMacro;
        uint32         SourceLine = 0;
        MString        AwaitExprText;
        MString        TargetVarName;
    };

    // spec §2.3: 跨 await 点仍然 live 的局部变量（用于状态机持久化）
    struct SLiveVarDecl {
        MString     Name;
        SParsedType Type;
        uint32      DeclLine         = 0;
        bool        bLiveAcrossAwait = false;
    };

    // spec §2.3: class/struct 反射记录（核心 IR 节点）
    struct SParsedRecord {
        ERecordKind              Kind = ERecordKind::Class;
        MString                  Name;
        MString                  QualifiedName;
        fs::path                 HeaderPath;
        uint32                   SourceLine = 0;
        MString                  ReflectionType;
        MString                  Owner;
        MString                  ParentClass;
        MString                  ClassFlagsExpr;
        TVector<MString>         AllParentClasses;
        bool                     bHasMGeneratedBody = false;
        bool                     bHasMClassMarker   = false;
        bool                     bHasMStructMarker  = false;
        TVector<SParsedProperty> Properties;
        TVector<SParsedFunction> Functions;
        TMap<MString, MString>   TypeAliases;
        bool                     bHasAsyncFunctions = false;
    };

    // spec §2.3: 反射函数（完整版）
    struct SParsedFunction {
        MString                   Name;
        MString                   QualifiedName;
        fs::path                  HeaderPath;
        uint32                    SourceLine = 0;
        SParsedType               ReturnType;
        TVector<SParsedParameter> Params;
        bool                      bConst    = false;
        EFunctionTransport        Transport = EFunctionTransport::None;
        ERpcKind                  RpcKind   = ERpcKind::None;
        bool                      bReliable = true;
        MString                   Endpoint;
        MString                   MessageName;
        MString                   Route;
        MString                   Target;
        MString                   Auth;
        MString                   Wrap;
        MString                   ClientApi;
        bool                      bHasAsyncMeta = false;
        bool                      bIsAsync      = false;
        MString                   AsyncBody;
        TVector<SAwaitSite>       AwaitSites;
        TVector<SLiveVarDecl>     LiveAcrossAwait;
        bool                      bHasValidate = false;
        TVector<MString>          DependencyList;
        bool                      bIsRpc       = false;
        bool                      bIsPlayerRpc = false;
    };

    // spec §2.3: 顶层 IR 容器
    struct SParseIR {
        int                       SchemaVersion = 1;
        MString                   CompileCommandsPath;
        TVector<SParsedRecord>    Records;
        TVector<SParsedEnum>      Enums;
        TVector<SParsedFunction>  FreeFunctions;
        TVector<SParsedTypeAlias> TypeAliases;
        TMap<MString, fs::path>   TypeToHeader;
        TSet<MString>             AllReflectedTypeNames;
    };

} // namespace mession::headercodegen
