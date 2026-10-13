/**
 * @file IR.h
 * @brief MHeaderTool 的 AST 中间表示(IR):反射类/枚举/函数/类型别名的规范化模型。
 *
 * @detail
 *   - 这是 `mession::headercodegen` 的**输出契约**:AST 遍历器把 Clang 语义写进这里,
 *     生成器再从这里读出、不再回头碰 Clang。
 *   - 相比工具层的 Core/Types.h,这里的类型是规范化后的:类型引用统一为 SParsedType,
 *     枚举/函数各自成节点,并用 enum class 表达传输/RPC/await 语义。
 *   - 纯数据聚合体(除前置声明的自引用外无方法),可自由拷贝;SchemaVersion 描述本结构
 *     的版本,升级不兼容字段时必须递增。
 *   - 大量字段只有短小描述;语义细节由消费者(生成器)解释,IR 本身不做校验。
 */
#pragma once

#include "Common/Runtime/MLib.h"
#include <cstdint>
#include <filesystem>

namespace fs = std::filesystem;

namespace mession::headercodegen {

    /**
     * @brief 记录类型分类:class 还是 struct(spec §2.3)。
     *
     * @detail 只影响生成产物里的反射类型名,不影响字段布局。
     */
    enum class ERecordKind : uint8_t { Class, Struct };

    /**
     * @brief MFUNCTION 的传输语义分类(spec §2.4)。
     *
     * @detail None = 普通成员函数;CallClient = 服务端向客户端发起的下行调用。
     */
    enum class EFunctionTransport : uint8_t { None, ServerCall, ClientCall, Client, ScriptBind, CallClient };

    /**
     * @brief RPC 调用方向(由 NetServer/NetClient 标记得出,spec §2.5)。
     *
     * @detail Server = 服务端发起,Client = 客户端发起,None = 未标注。
     */
    enum class ERpcKind : uint8_t { None, Server, Client };

    /**
     * @brief 枚举是普通 enum 还是 scoped enum(`enum class`)(spec §2.6)。
     */
    enum class EEnumKind : uint8_t { Plain, Scoped };

    /**
     * @brief 异步等待点种类(spec §2.7):AWAIT_OK 宏 / TAwaitable<> 调用 / 裸 Awaitable。
     *
     * @detail 状态机生成器据此决定如何保存与恢复挂起点。
     */
    enum class EAwaitSiteKind : uint8_t { AwaitOkMacro, TAwaitableCall, AwaitableBare };

    /**
     * @brief 规范化后的类型:类型名 + 模板实参 + 引用/const/指针标记(spec §2.3)。
     *
     * @detail CanonicalName 只写"不带模板实参"的类型名;模板实参在 TemplateArgs 里递归展开。
     *         ResolvedClassName 是解析到具体反射类后的类名,无法解析时为空。
     */
    struct SParsedType {
        MString              CanonicalName;
        bool                 bReference = false;
        bool                 bConst     = false;
        bool                 bPointer   = false;
        TVector<SParsedType> TemplateArgs;
        MString              ResolvedClassName;
    };

    /**
     * @brief 函数形参:形参名 + 规范化类型(spec §2.3)。
     */
    struct SParsedParameter {
        MString     Name;
        SParsedType Type;
    };

    /**
     * @brief 单条 MPROPERTY Meta=(K=V,...) 元数据(spec §2.3)。
     *
     * @detail 字段与 Core/Types.h 的 SMetadataEntry 一一对应,ToLegacyProperty 时逐字段拷贝。
     */
    struct SMetadataEntry {
        MString Key;
        MString Value;
    };

    /**
     * @brief 一个 MPROPERTY 反射字段(spec §2.3)。
     *
     * @detail Name/Type 直接对应 C++ 字段声明,是公开 ABI 的一部分;Owner 表示字段声明所在类。
     */
    struct SParsedProperty {
        MString     Name;
        SParsedType Type;
        MString     Owner;
        MString     FlagsExpr;

        TVector<SMetadataEntry> Metadata;
    };

    /**
     * @brief enum 的一个取值:名字 + 显式数值(spec §2.3)。
     */
    struct SParsedEnumValue {
        MString Name;
        int64   Value = 0;
    };

    /**
     * @brief 一个完整 enum:种类 + 基础类型 + 取值列表(spec §2.3)。
     *
     * @detail Values 与 ValuesDetailed 是同一批取值的两种视图:前者只要名字(供文本生成),
     *         后者带数值(供计算校验)。EnumKind 与 bScopedEnum 描述同一件事,须保持一致。
     */
    struct SParsedEnum {
        EEnumKind                 EnumKind = EEnumKind::Plain;
        MString                   Name;
        MString                   QualifiedName; // namespace 全名(如 mession::script::EScriptLanguage)
        MString                   Owner;         // 类内 enum 的所属类名(私有 enum 值不可外部引用)
        fs::path                  HeaderPath;
        MString                   UnderlyingType;
        TVector<MString>          Values;
        TVector<SParsedEnumValue> ValuesDetailed;
        bool                      bScopedEnum = false;
    };

    /**
     * @brief 一条 `using A = B;` 类型别名(spec §2.3)。
     */
    struct SParsedTypeAlias {
        MString  Name;
        MString  UnderlyingType;
        fs::path HeaderPath;
    };

    /**
     * @brief 反射函数:RPC / client surface / async / 普通成员函数(spec §2.3)。
     *
     * @detail 前置声明的原因:SParsedRecord 与 SParseIR 都用 TVector<SParsedFunction> 持有
     *         函数(故 SParsedFunction 必须完整定义才能实例化容器),而函数节点又反过来
     *         引用 SParsedRecord 才需要的类型元信息 —— 先声明、后定义可打破这个环。
     */
    struct SParsedFunction;

    /**
     * @brief 单个 await 挂起点(spec §2.3)。
     */
    struct SAwaitSite {
        EAwaitSiteKind Kind       = EAwaitSiteKind::AwaitOkMacro;
        uint32         SourceLine = 0;
        MString        AwaitExprText;
        MString        TargetVarName;
    };

    /**
     * @brief 跨 await 点仍然存活的局部变量:状态机生成时必须序列化进 Frame(spec §2.3)。
     */
    struct SLiveVarDecl {
        MString     Name;
        SParsedType Type;
        uint32      DeclLine         = 0;
        bool        bLiveAcrossAwait = false;
    };

    /**
     * @brief class/struct 反射记录:IR 的核心节点(spec §2.3)。
     *
     * @detail 三个 bHas* 标记区分 MCLASS/MSTRUCT/MGENERATED_BODY 的写法;Owner 表示嵌套类的
     *         外层类名;ParentClass 为直接基类。函数与属性都按源码出现顺序追加。
     */
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

    /**
     * @brief 反射函数完整定义(spec §2.3):签名 + 传输/RPC 语义 + async 状态机信息。
     *
     * @detail 语义字段来源:Transport/RpcKind 来自 MFUNCTION 的裸标记;Endpoint/MessageName/
     *         Route/Target/Auth/Wrap/ClientApi 来自 "Key=Value" 形式参数(原样保留字符串)。
     *         AwaitSites/LiveAcrossAwait 只有 async 函数才有内容。
     */
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

    /**
     * @brief 一次解析的顶层 IR 容器(spec §2.3)。
     *
     * @detail TypeToHeader 是"类型名 → 声明头文件"的反查表,供生成器写 include;
     *         AllReflectedTypeNames 是全部被反射类型名的集合,用于快速判定"是否已反射"。
     *         两者都由 AST 阶段填充,生成阶段只读。
     */
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
