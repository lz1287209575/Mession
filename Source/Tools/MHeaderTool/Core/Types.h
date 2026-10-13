/**
 * @file Types.h
 * @brief MHeaderTool 的输入/输出数据模型:解析结果结构体、校验规则与运行选项。
 *
 * @detail
 *   - 命名空间是 MHeaderTool(不是 mession::headercodegen):这里是**工具层**的数据模型,
 *     由 AST 层(IR.h)转换后喂给 CodeGenerator。
 *   - 所有 MString 字段存的都是**原文文本片段**(宏参数原文、类型拼写、签名),本身不做
 *     语义校验;合法性由下游代码生成器与编译器负责。
 *   - 全部是聚合体(aggregate):无方法、无所有权语义、可自由拷贝。
 */
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace MHeaderTool {

    // ============================================================================
    // 枚举
    // ============================================================================

    /**
     * @brief 被解析的顶层类型种类。
     *
     * @detail 决定生成器走 class 路径还是 enum 路径;struct 仅影响反射类型名("Struct" / "Class")。
     */
    enum class EParsedTypeKind : uint8_t { Class, Struct, Enum };

    // ============================================================================
    // 校验规则
    // ============================================================================

    /**
     * @brief 一条字段级校验规则:字段名 + 校验器名。
     *
     * @detail 由 MFUNCTION 的 Validate=/Meta 参数解析而来,最终写入校验协议 Schema 供运行时校验器查表。
     */
    struct SValidationRule {
        MString FieldName;     // 被校验的目标字段名(在所属类/消息内)
        MString ValidatorName; // 校验器注册名(runtime 侧查表用)
    };

    // ============================================================================
    // 解析结果类型
    // ============================================================================

    /**
     * @brief 反射函数/属性的单个形参(mession-codegen 语法糖解析结果)。
     */
    struct SParsedParameter {
        MString Type;         // 形参在源码里的原始类型拼写
        MString StorageType;  // 去掉引用/const 等修饰后的存储类型(生成 stub 时声明变量用)
        MString Name;         // 形参名
        MString PropertyKind; // 属性语义标记(如 Ref/Out),空 = 普通入参
    };

    /**
     * @brief MPROPERTY Meta=(K=V,...) 解析后的一条键值元数据。
     *
     * @detail 裸标记(如 Meta=(NonZero))在解析阶段就被归一成 Value="true"。
     */
    struct SMetadataEntry {
        MString Key;   // Meta 键(如 Cli / Range / NonZero)
        MString Value; // Meta 值;裸标记为 "true"
    };

    /**
     * @brief 一个 MPROPERTY 反射字段的解析结果。
     *
     * @detail 字段名/顺序/类型一旦进入生成产物就是公开 ABI,不要在这里做任何归一化改写。
     */
    struct SParsedProperty {
        MString                 MacroArgs;    // MPROPERTY(...) 括号内原文
        MString                 Type;         // 字段类型拼写
        MString                 Name;         // 字段名(ABI 的一部分)
        MString                 PropertyKind; // 属性语义标记,如 Ref/Transient
        MString                 FlagsExpr;    // 反射 Flag 表达式(C++ 表达式文本,原样透传)
        MString                 Owner;        // 所属类名
        TVector<SMetadataEntry> Metadata;     // Meta=(...) 解析出的键值对,顺序保持源码顺序
    };

    /**
     * @brief 一个 MFUNCTION 反射函数的解析结果(旧版数据模型)。
     *
     * @detail 传输/RPC 语义字段(Transport / RpcKind / bIsRpc / bIsPlayerRpc)是从 MFUNCTION
     *         括号里的裸标记推导出来的;带 "=" 的参数(Message= / Target= 等)存为文本,
     *         生成器不解释其含义,只透传给协议层。
     */
    struct SParsedFunction {
        MString                   MacroArgs;
        MString                   ReturnType;
        MString                   ReturnStorageType;
        MString                   ReturnPropertyKind;
        MString                   Name;
        MString                   Signature;
        MString                   Owner;
        bool                      bConst       = false; // 是否 const 成员函数
        bool                      bHasValidate = false; // 是否带 Validate 校验规则
        bool                      bIsAsync     = false; // 是否 async 函数
        bool                      bIsRpc       = false; // 是否 RPC(任一 RPC 标记或传输标记都会置位)
        bool                      bIsPlayerRpc = false; // 是否 PlayerRPC
        bool                      bReliable    = true;  // 是否可靠传输(默认可靠)
        MString                   AsyncBody;            // Async 函数的函数体原文(含 AWAIT 宏),仅 bIsAsync 时有值
        MString                   Transport;            // MFUNCTION 传输标记原文:"ServerCall"/"ClientCall"/"CallClient"/"Client"/"ScriptBind",无标记时为 ""
        MString                   RpcKind;              // MFUNCTION 的 NetServer/NetClient 标记原文,无则为 ""
        MString                   Endpoint;             // MFUNCTION(Endpoint="...") 参数
        MString                   MessageName;          // MFUNCTION(Message="...") 参数:对应协议消息结构体名
        MString                   Route;                // MFUNCTION(Route="...") 参数
        MString                   Target;               // MFUNCTION(Target="...") 参数:目标 Actor 类型名
        MString                   Auth;                 // MFUNCTION(Auth="...") 参数:鉴权模式
        MString                   Wrap;                 // MFUNCTION(Wrap="...") 参数:返回值包装形式
        MString                   ClientApi;            // MFUNCTION(Api="..." / ClientApi="...") 参数:客户端暴露名
        TVector<SParsedParameter> Params;
        TVector<SValidationRule>  ValidationRules;
        TVector<MString>          DependencyList;
    };

    /**
     * @brief 一个被反射的 class/struct/enum 的解析结果(旧版数据模型)。
     *
     * @detail Kind 复用 EParsedTypeKind;enum 走 bScopedEnum + EnumValues,类走 Properties/Functions。
     *         Kind 与 bScopedEnum 都描述 enum,取值需保持一致(解析侧同时设置)。
     */
    struct SParsedClass {
        EParsedTypeKind          Kind = EParsedTypeKind::Class;
        MString                  Name;
        fs::path                 HeaderPath;
        size_t                   SourceLine     = 0;         // 行号，用于去重
        MString                  ParentClass    = "MObject"; // 直接基类;无显式基类时默认 MObject
        MString                  ClassFlagsExpr = "0";       // 类级反射 Flag 表达式文本
        MString                  ReflectionType = "Object";  // 反射类型名("Object"/"Struct"/"Enum")
        MString                  Owner;
        MString                  QualifiedName;                // namespace 全名(enum/类;namespace 级 scoped enum 用)
        TVector<MString>         AllParentClasses;             // 所有基类，用于检查是否继承自 MServerCallProxyBase
        bool                     bScopedEnum        = false;   // 是否 enum class(scoped enum)
        MString                  EnumUnderlyingType = "int32"; // scoped enum 的基础类型;未显式写定时为 int32
        TMap<MString, MString>   TypeAliases;
        TVector<SParsedProperty> Properties;
        TVector<SParsedFunction> Functions;
        TVector<MString>         EnumValues;
    };

    // ============================================================================
    // 自由异步函数(P4 — spec 2026-07-28 §B)
    // ============================================================================
    //
    /**
     * @brief 一个命名空间作用域的 `MFUNCTION(Async)` 自由函数。
     *
     * @detail 与 SParsedFunction 不同:自由函数没有所属类,只需要头文件路径 + 函数身份。
     *         被 `MCodeGenerator::EmitFreeAsyncFramesHeader` 消费,为每个自由异步函数
     *         在 `<Header>_FreeAsyncFrames.mgenerated.h` 里生成一个 Frame 结构体。
     *
     *         注意:此结构体不能携带 Transport/RPC 相关信息 —— 自由函数不存在
     *         "类方法"语义,带传输标记的自由函数会被解析阶段直接拒绝。
     */
    struct SFreeAsyncFunc {
        fs::path HeaderPath;   // 声明所在头文件;生成时按此分文件聚合
        MString  Name;         // 函数名(不含命名空间)
        MString  ResponseType; // 返回值类型拼写(状态机完成后的结果类型)
        MString  AsyncBody;    // 函数体原文,状态机生成器的输入
    };

    // ============================================================================
    // 校验协议 Schema
    // ============================================================================

    /**
     * @brief 校验协议 Schema 里的一个字段条目(输出用,不参与生成决策)。
     *
     * @detail TypeName/ItemKind/ItemTypeName/Size 都是可选项:标量只有 TypeName,
     *         容器才有 Item*;Size 仅对定长数组有意义。
     */
    struct SValidationSchemaField {
        MString            Name;
        MString            Kind;
        TOptional<MString> TypeName;
        TOptional<MString> ItemKind;
        TOptional<MString> ItemTypeName;
        TOptional<size_t>  Size;
    };

    // ============================================================================
    // 运行配置
    // ============================================================================

    /**
     * @brief MHeaderTool 的运行选项:输入/输出路径 + 增量与并发开关。
     *
     * @detail 默认值是"仓库内直接跑"的约定值(相对仓库根):Source 为输入,
     *         Build/Generated 为产物,缓存放 Build/.mheadertool_cache。
     *         所有路径由命令行参数覆盖,生成器本身不校验路径是否存在。
     */
    struct SOptions {
        fs::path SourceRoot                 = "Source";
        fs::path OutputDir                  = "Build/Generated";
        fs::path CacheDir                   = "Build/.mheadertool_cache";
        fs::path CMakeManifestPath          = "Build/Generated/MHeaderToolTargets.cmake";
        fs::path ValidationSchemaPath       = "Build/Generated/ValidationProtocolSchema.json";
        fs::path ClientManifestPath         = "Build/Generated/MClientManifest.mgenerated.cpp";
        fs::path ClientDownlinkManifestPath = "Build/Generated/MClientDownlinkManifest.mgenerated.cpp";
        fs::path ClientDownlinkHeaderPath   = "Build/Generated/MClientDownlinkManifest.mgenerated.h";
        fs::path MemberRpcManifestPath      = "Build/Generated/MemberRpcManifest.mgenerated.cpp";
        fs::path LuaStdlibHintLuaPath       = "Build/Generated/Mession.lua";
        fs::path LuaStdlibHintTealPath      = "Build/Generated/Mession.d.tl";
        bool     bVerbose                   = false;
        bool     bIncremental               = true;
        bool     bForceFull                 = false;
        bool     bBenchmark                 = false;
        bool     bDryRun                    = false;
        int      NumThreads                 = 0; // 0 = 自动(按 CPU 核数)
    };

    // ============================================================================
    // 类型别名
    // ============================================================================

    // 类名 → 该类待生成的 RPC 列表宏条目:生成 RPC 注册宏时按类聚合。
    using TRpcListMacroMap = TMap<MString, TVector<SParsedFunction>>;

} // namespace MHeaderTool
