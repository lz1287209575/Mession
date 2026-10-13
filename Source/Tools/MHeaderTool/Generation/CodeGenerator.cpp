/**
 * @file CodeGenerator.cpp
 * @brief MCodeGenerator 的 IR 入口实现:把 SParseIR 节点转成 legacy 视图后交给原有生成逻辑。
 *
 * @detail 本 TU 只有少量成员函数定义,策略是"**转换 + 委派**":
 *   1. 先把 IR 节点(SParsedRecord / SParsedFunction / SParsedProperty / SParsedEnum)转换
 *      成 legacy 的 SParsedClass 家族(anonymous namespace 里的 ToLegacy* 系列);
 *   2. 再原样调用 CodeGenerator.h 里那批内联生成函数(GenerateHeader / GenerateSource /
 *      GenerateAsyncStateMachine ...)。
 *   这样做的目的是让 AST 路径与旧的字符串解析路径产出**逐字节一致**的文件(由
 *   Tests/A2DiffTest.cpp 把关)。shim 只填那批生成函数真正读到的字段(见各 ToLegacy* 注释),
 *   IR 独有的字段(bHasAsyncFunctions / AwaitSites / LiveAcrossAwait 等)不参与转换。
 *
 *   NOTE: 这是 A2 迁移期的过渡形态 —— 真正的生成逻辑仍在头文件里,本文件只做适配层。
 *   让生成逻辑直接消费 IR(并删除 legacy 实现)是范围更大的后续改动,不在本阶段。
 */

#include "Generation/CodeGenerator.h"

#include "Common/Runtime/MLib.h"

#include <sstream>
#include <string>
#include <vector>

namespace MHeaderTool {

    namespace {

        // TU 内部别名:生成器统一用 MStringStream 表示输出流。
        // MLib 只暴露 MString、没有 MStringStream,与其为一个名字去改公共头,
        // 不如把别名限制在本 TU 内,保持 .cpp 自包含。
        using MStringStream = std::ostringstream;

        /**
         * @brief 从源文件路径推导 manifest 分组用的 Owner。
         * @param HeaderPath 头文件路径(绝对路径或仓库相对路径都行)
         * @return 路径里首个 `/Source/Servers/<Service>/` 的 <Service>;没命中则返回 "Shared"
         *
         * @detail 对齐旧 Parsing/ClassParser.h::DetermineOwnerFromHeaderPath。
         *         Owner 会当成 ManifestGenerators 的 CMake generated group 名
         *         (echoservice / gateway / serviceregistry / shared);一旦推错,所有类型都会挤进
         *         shared 组,链接任一服务时就会引用到别的服务的业务符号(vtable / ServerCall 实现),
         *         报 undefined reference。
         *         匹配串带前导 '/',因此绝对路径与仓库相对路径都能命中。
         */
        MString DetermineOwnerFromHeaderPath(const fs::path& HeaderPath) {
            const MString P      = HeaderPath.generic_string();
            const MString Needle = "/Source/Servers/";
            const size_t  Pos    = P.find(Needle);
            if (Pos != MString::npos) {
                const size_t NameStart = Pos + Needle.size();
                const size_t NameEnd   = P.find('/', NameStart);
                if (NameEnd != MString::npos) {
                    return P.substr(NameStart, NameEnd - NameStart);
                }
            }
            return "Shared";
        }

        /**
         * @brief 把规范化类型名映射成 legacy 生成器要用的 EPropertyType 后缀。
         * @param TypeName 类型名,允许带空白(内部先去掉全部空白再比对)
         * @return "Int8".."UInt64" / "Float" / "Double" / "Bool" / "String" / "Name" /
         *         "Vector" / "Rotator" / "Array";都不匹配时返回 "Struct"
         *
         * @detail 对齐旧 CodeGenerator.h::InferPropertyKind —— legacy 生成器拿这个后缀拼
         *         `EPropertyType::<Kind>` 传给 CreateOffsetProperty。
         *
         * @detail NOTE: 两处宽松,与实现一致 ——
         *   1. 判断 `TByteArray<` 的那一项基本命中不了(TByteArray 本身是不带模板参数的别名),
         *      属于冗余条件;
         *   2. 认不出来的类型一律归成 "Struct"(含指针、自定义类型),不报错也不告警。
         */
        MString InferPropertyKindCompat(const MString& TypeName) {
            MString Compact = TypeName;
            Compact.erase(std::remove_if(Compact.begin(), Compact.end(), [](char C) { return std::isspace(static_cast<unsigned char>(C)); }), Compact.end());
            if (Compact == "int8")
                return "Int8";
            if (Compact == "int16")
                return "Int16";
            if (Compact == "int32")
                return "Int32";
            if (Compact == "int64")
                return "Int64";
            if (Compact == "uint8")
                return "UInt8";
            if (Compact == "uint16")
                return "UInt16";
            if (Compact == "uint32")
                return "UInt32";
            if (Compact == "uint64")
                return "UInt64";
            if (Compact == "float")
                return "Float";
            if (Compact == "double")
                return "Double";
            if (Compact == "bool")
                return "Bool";
            if (Compact == "MString")
                return "String";
            if (Compact == "MName")
                return "Name";
            if (Compact == "SVector")
                return "Vector";
            if (Compact == "SRotator")
                return "Rotator";
            if (Compact.rfind("TVector<", 0) == 0 || Compact.rfind("TByteArray<", 0) == 0 || Compact.rfind("TMap<", 0) == 0 || Compact.rfind("TSet<", 0) == 0) {
                return "Array";
            }
            return "Struct";
        }

        /**
         * @brief 规范化反射类型名:去首尾空白、剥 `const ` 前缀、去尾随 `&` / `*`。
         * @param TypeName 类型原文(按值传入,内部反复裁剪)
         * @return 规范化结果
         *
         * @detail 对齐旧 Parsing/FunctionParser.h 的同名函数。legacy 生成器用 StorageType
         *         (即本函数结果)去算 include 与类型名,用 Type(源码拼写)去写参数声明。
         *         只剥最外层:`const T& const` 这类内层限定不会被处理。
         */
        MString NormalizeReflectionType(MString TypeName) {
            auto Trim = [](MString In) {
                const size_t B = In.find_first_not_of(" \t\r\n");
                if (B == MString::npos)
                    return MString();
                const size_t E = In.find_last_not_of(" \t\r\n");
                return In.substr(B, E - B + 1);
            };
            TypeName = Trim(TypeName);
            while (TypeName.rfind("const ", 0) == 0) {
                TypeName = Trim(TypeName.substr(6));
            }
            while (!TypeName.empty() && (TypeName.back() == '&' || TypeName.back() == '*')) {
                TypeName.pop_back();
                TypeName = Trim(TypeName);
            }
            return TypeName;
        }

        // ---------------------------------------------------------------------------
        // IR → legacy SParsedClass 适配层
        // ---------------------------------------------------------------------------
        //
        // 只把"内联生成函数真正会读到"的 IR 字段搬进 legacy 结构体,然后原样调用那批函数。
        // IR 独有的字段(bHasAsyncFunctions / AwaitSites / LiveAcrossAwait 等)生成器用不到,
        // 因此不转换 —— 这也是 AST 路径能与旧路径产出逐字节相同文件的原因(A2DiffTest 把关)。

        /**
         * @brief IR 属性 → legacy SParsedProperty。
         * @param In IR 属性
         * @return legacy 属性;只填 legacy 生成函数会读的字段
         */
        SParsedProperty ToLegacyProperty(const mession::headercodegen::SParsedProperty& In) {
            SParsedProperty Out;
            Out.Name      = In.Name;
            Out.Type      = In.Type.CanonicalName;
            Out.FlagsExpr = In.FlagsExpr;
            // 把 FlagsExpr 同时写进 MacroArgs:legacy 的 BuildPropertyFlagsExpr 扫的是
            // MacroArgs 的 token 流而不是 FlagsExpr。不同步的话,带标记的
            // MPROPERTY(PersistentData|Replicated) 在注册属性时一律退化成 EPropertyFlags::None。
            Out.MacroArgs = In.FlagsExpr;
            Out.Owner     = In.Owner;
            for (const auto& M : In.Metadata) {
                Out.Metadata.push_back({M.Key, M.Value});
            }
            return Out;
        }

        /**
         * @brief IR 函数 → legacy SParsedFunction。
         * @param In IR 函数
         * @return legacy 函数;transport/rpcKind 由强类型枚举还原成小写风格字符串
         */
        SParsedFunction ToLegacyFunction(const mession::headercodegen::SParsedFunction& In) {
            SParsedFunction Out;
            Out.Name           = In.Name;
            Out.bConst         = In.bConst;
            Out.bIsAsync       = In.bIsAsync;
            Out.bIsRpc         = In.bIsRpc;
            Out.bIsPlayerRpc   = In.bIsPlayerRpc;
            Out.bReliable      = In.bReliable;
            Out.AsyncBody      = In.AsyncBody;
            Out.MessageName    = In.MessageName;
            Out.Route          = In.Route;
            Out.Target         = In.Target;
            Out.Auth           = In.Auth;
            Out.Wrap           = In.Wrap;
            Out.ClientApi      = In.ClientApi;
            Out.Endpoint       = In.Endpoint;
            Out.DependencyList = In.DependencyList;

            // IR 侧是强类型枚举,legacy 生成器只认字符串标记;这里还原成与旧字符串解析
            // 路径完全相同的词汇表,保证两条路径的产物一致。
            switch (In.Transport) {
            case mession::headercodegen::EFunctionTransport::ServerCall:
                Out.Transport = "ServerCall";
                break;
            case mession::headercodegen::EFunctionTransport::ClientCall:
                Out.Transport = "ClientCall";
                break;
            case mession::headercodegen::EFunctionTransport::CallClient:
                Out.Transport = "CallClient";
                break;
            case mession::headercodegen::EFunctionTransport::Client:
                Out.Transport = "Client";
                break;
            case mession::headercodegen::EFunctionTransport::ScriptBind:
                Out.Transport = "ScriptBind";
                break;
            case mession::headercodegen::EFunctionTransport::None:
            default:
                Out.Transport = "";
                break;
            }
            switch (In.RpcKind) {
            case mession::headercodegen::ERpcKind::Server:
                Out.RpcKind = "Server";
                break;
            case mession::headercodegen::ERpcKind::Client:
                Out.RpcKind = "Client";
                break;
            case mession::headercodegen::ERpcKind::None:
            default:
                Out.RpcKind = "";
                break;
            }

            // Return type — IR 存源码拼写（QualTypeToSParsedType + TypeSourceInfo）；
            // legacy 期望 Type = 源码拼写、ReturnStorageType = 规范化名（去 const/&）。
            Out.ReturnType        = In.ReturnType.CanonicalName;
            Out.ReturnStorageType = NormalizeReflectionType(In.ReturnType.CanonicalName);

            // 参数 — SParsedParameter → SParsedParameter(legacy)。
            for (const auto& P : In.Params) {
                SParsedParameter LP;
                LP.Name        = P.Name;
                LP.Type        = P.Type.CanonicalName;
                LP.StorageType = NormalizeReflectionType(P.Type.CanonicalName);
                // legacy 生成器 CreateOffsetProperty 直接拼 EPropertyType::<PropertyKind>
                LP.PropertyKind = InferPropertyKindCompat(LP.StorageType);
                Out.Params.push_back(std::move(LP));
            }

            return Out;
        }

        /**
         * @brief IR 记录 → legacy SParsedClass。
         * @param In IR 记录(类 / 结构体)
         * @return legacy 类视图;属性与函数递归走 ToLegacyProperty / ToLegacyFunction
         *
         * @detail 有三处**兜底默认值**,它们决定了产物内容,改动前务必确认:
         *   ParentClass 为空 → "MObject";ClassFlagsExpr 为空 → "0";ReflectionType 为空 → "Object"。
         *   Owner 为空时会用 DetermineOwnerFromHeaderPath 从路径反推(manifest 分组键)。
         */
        SParsedClass ToLegacyClass(const mession::headercodegen::SParsedRecord& In) {
            SParsedClass Out;
            Out.Kind             = (In.Kind == mession::headercodegen::ERecordKind::Struct) ? EParsedTypeKind::Struct : EParsedTypeKind::Class;
            Out.Name             = In.Name;
            Out.HeaderPath       = In.HeaderPath;
            Out.ParentClass      = In.ParentClass.empty() ? "MObject" : In.ParentClass;
            Out.ClassFlagsExpr   = In.ClassFlagsExpr.empty() ? "0" : In.ClassFlagsExpr;
            Out.ReflectionType   = In.ReflectionType.empty() ? "Object" : In.ReflectionType;
            Out.Owner            = In.Owner;
            Out.AllParentClasses = In.AllParentClasses;
            Out.TypeAliases      = In.TypeAliases;

            for (const auto& P : In.Properties) {
                Out.Properties.push_back(ToLegacyProperty(P));
            }
            for (const auto& F : In.Functions) {
                Out.Functions.push_back(ToLegacyFunction(F));
            }
            // manifest 分组键（旧字符串解析路径有同名推导）
            if (Out.Owner.empty()) {
                Out.Owner = DetermineOwnerFromHeaderPath(Out.HeaderPath);
            }
            return Out;
        }

    } // namespace

    /**
     * @brief 生成一个类型的 .mgenerated.h 内容。
     * @param Record                目标记录(类或结构体)
     * @param AllRecordsInSameHeader 同名头文件内的其它记录 —— **当前未使用**(legacy 生成函数也不读)
     * @return 头文件全文
     *
     * @detail 先写固定抬头(#pragma once、生成标记、来源路径、公共 include 与用户头),
     *         再经适配层分派到 legacy 的 GenerateStructHeader / GenerateClassHeader。
     *         MSTRUCT 且父类也是反射 struct 时,额外 include 父类的 .mgenerated.h ——
     *         继承请求链的 SetParent 需要父类的注册函数声明(actor-member-framework §5)。
     */

    MString MCodeGenerator::GenerateHeaderFromIR(const SParsedRecord& Record, const TVector<SParsedRecord>& /*AllRecordsInSameHeader*/) const {
        MStringStream Out;
        Out << "#pragma once\n";
        Out << "// Generated by MHeaderTool\n";
        Out << "// Source: " << Record.HeaderPath.string() << "\n";
        Out << "// Reflected " << (Record.Kind == mession::headercodegen::ERecordKind::Struct ? "struct" : "class") << ": " << Record.Name << "\n";
        Out << "\n";

        Out << "#include \"Common/Runtime/Reflect/Reflection.h\"\n";
        Out << "#include \"Common/Runtime/Async/MAsync.h\"\n";
        Out << "#include \"Common/Net/Rpc/RpcClientCall.h\"\n";
        // 与 legacy GenerateHeader 一致：include 用户头前不额外空行
        Out << "#include \"" << MakeIncludePathFromHeader(Record.HeaderPath) << "\"\n";
        // F0 actor-member-framework §5:继承请求链的 SetParent 需要父类注册函数
        // 声明——MSTRUCT 父类的 MHeaderTool_Generated_RegisterStruct_<P> 在父类
        // 的 .mgenerated.h,这里显式 include(父类是反射 struct 时)。
        {
            const SParsedClass& ParentProbe = ToLegacyClass(Record);
            if (ParentProbe.Kind == EParsedTypeKind::Struct) {
                auto ParentIt = ReflectedClassKinds.find(ParentProbe.ParentClass);
                if (ParentIt != ReflectedClassKinds.end() && ParentIt->second) {
                    Out << "#include \"" << SanitizeIdentifier(ParentProbe.ParentClass) << ".mgenerated.h\"\n";
                }
            }
        }
        Out << "\n";

        SParsedClass Legacy = ToLegacyClass(Record);
        if (Legacy.Kind == EParsedTypeKind::Struct) {
            GenerateStructHeader(Out, Legacy);
        } else {
            GenerateClassHeader(Out, Legacy);
        }
        return Out.str();
    }

    /**
     * @brief 生成一个类型的 .mgenerated.cpp 内容。
     * @param Record 目标记录(类或结构体)
     * @return 源文件全文
     *
     * @detail 形态与 legacy GenerateSource 一致:生成标记 + 来源路径 + include 对应的
     *         .mgenerated.h 与用户头,再经适配层分派到 GenerateStructSource / GenerateClassSource。
     */

    MString MCodeGenerator::GenerateSourceFromIR(const SParsedRecord& Record) const {
        MStringStream Out;
        const MString IncludeName     = MakeIncludePathFromHeader(Record.HeaderPath);
        const MString GeneratedHeader = SanitizeIdentifier(Record.Name) + ".mgenerated.h";

        Out << "// Generated by MHeaderTool\n";
        Out << "// Source: " << Record.HeaderPath.string() << "\n";
        Out << "#include \"" << GeneratedHeader << "\"\n";
        Out << "#include \"" << IncludeName << "\"\n";
        Out << "\n\n";

        SParsedClass Legacy = ToLegacyClass(Record);
        if (Legacy.Kind == EParsedTypeKind::Struct) {
            GenerateStructSource(Out, Legacy);
        } else {
            GenerateClassSource(Out, Legacy);
        }
        return Out.str();
    }

    /**
     * @brief 生成 `<ClassName>_AsyncFrames.h`(async 函数用的 Frame 结构体定义)。
     * @param Record 目标记录
     * @return 文件全文;不是 class、或没有任何 async 函数时返回**空串**(调用方据此跳过落盘)
     *
     * @detail Frame 结构体必须放在用户头**之前**,内联的 async 函数体才能在 AWAIT_OK 里构造并使用
     *         它,所以单独出一个文件而不是塞进 .mgenerated.h(.mgenerated.h 反过来 include 用户头,
     *         会形成循环 include)。
     */

    MString MCodeGenerator::EmitAsyncFramesHeaderFromIR(const SParsedRecord& Record) const {
        // AsyncFrames 只对 class 有意义(enum / struct 直接空返回)。
        if (Record.Kind != mession::headercodegen::ERecordKind::Class) {
            return {};
        }

        bool bHasAsync = false;
        for (const auto& func : Record.Functions) {
            if (func.bIsAsync) {
                bHasAsync = true;
                break;
            }
        }
        if (!bHasAsync)
            return {};

        MStringStream Out;
        Out << "#pragma once\n";
        Out << "// Generated by MHeaderTool\n";
        Out << "// Source: " << Record.HeaderPath.string() << "\n";
        Out << "// Async Frame struct definitions for " << Record.Name << "\n";
        Out << "// (P3 v1 inline-body design — see spec 2026-07-24 §7.3)\n";
        Out << "//\n";
        Out << "// The user header must #include this file BEFORE `class " << Record.Name << "` so inline async bodies can construct\n";
        Out << "// and use the Frame via AWAIT_OK.\n";
        Out << "\n";

        Out << "#include \"Common/Runtime/Async/MAsync.h\"\n";
        Out << "#include \"Common/Runtime/Reflect/Reflection.h\"\n";
        Out << "\n";

        SParsedClass Legacy = ToLegacyClass(Record);
        for (const auto& func : Legacy.Functions) {
            if (func.bIsAsync) {
                GenerateAsyncStateMachine(Out, Legacy, func);
            }
        }

        return Out.str();
    }

    SParsedClass ToLegacyEnum(const mession::headercodegen::SParsedEnum& In); // 定义在下方

    // ---------------------------------------------------------------------------
    // ToLegacyClasses — 整树 IR → legacy SParsedClass 集合
    // ---------------------------------------------------------------------------
    //
    // ManifestGenerators / ScriptBindEmitter 消费的仍是 legacy SParsedClass(Core/Types.h),
    // 而 AST 路径产出 SParseIR。这里复用 anonymous namespace 里的 ToLegacyClass(同 TU 可见)
    // 逐 Record 转换,与 GenerateHeaderFromIR 内部的单 Record 转换完全一致。
    /**
     * @brief 整树 IR → legacy 类集合(只含类/结构体)。
     * @param IR 解析出的 IR 树
     * @return 与 IR.Records 一一对应、顺序相同的 legacy 视图
     *
     * @detail **不含 enum** —— 需要 enum 的调用方(CMake manifest)请用 ToLegacyClassesWithEnums,
     *         其余 manifest 消费只关心 Records。
     */
    TVector<SParsedClass> MCodeGenerator::ToLegacyClasses(const mession::headercodegen::SParseIR& IR) const {
        // 只含 Records（类/结构）。enum 走 ToLegacyClassesWithEnums（CMake manifest
        // 需要 namespace enum 的注册单元编译；其它 manifest 消费仍只用 Records）。
        TVector<SParsedClass> Out;
        Out.reserve(IR.Records.size());
        for (const auto& Record : IR.Records) {
            Out.push_back(ToLegacyClass(Record));
        }
        return Out;
    }

    /**
     * @brief ToLegacyClasses 的结果再追加 enum 的 legacy 视图。
     * @param IR 解析出的 IR 树
     * @return Records 在前、Enums 在后的完整 legacy 集合
     *
     * @detail 只有 CMake manifest 需要这份"全量" —— namespace enum 的注册单元必须被编译进去。
     */
    TVector<SParsedClass> MCodeGenerator::ToLegacyClassesWithEnums(const mession::headercodegen::SParseIR& IR) const {
        TVector<SParsedClass> Out = ToLegacyClasses(IR);
        for (const auto& Enum : IR.Enums) {
            Out.push_back(ToLegacyEnum(Enum));
        }
        return Out;
    }

    // ---------------------------------------------------------------------------
    // ToLegacyEnum — SParsedEnum → legacy SParsedClass(Enum) 适配层
    // ---------------------------------------------------------------------------
    /**
     * @brief IR enum → legacy SParsedClass(Kind = Enum)。
     * @param In IR 枚举
     * @return Kind/ReflectionType 已按 legacy 约定填好的类视图
     *
     * @detail legacy 的 GenerateHeader/GenerateSource 按 SParsedClass.Kind == Enum 分派到
     *         GenerateEnumHeader / GenerateEnumSource。AST 侧 SParseIR::Enums 只填了
     *         Name / QualifiedName / Owner / HeaderPath / bScopedEnum / Values。
     *         Owner 通常是空(namespace 级 scoped enum),此时 GenerateEnumHeader 会用
     *         QualifiedName 生成真实注册(mession::script::EScriptLanguage::Value 之类),
     *         而不是退化版 stub。
     */
    SParsedClass ToLegacyEnum(const mession::headercodegen::SParsedEnum& In) {
        SParsedClass Out;
        Out.Name           = In.Name;
        Out.QualifiedName  = In.QualifiedName;
        Out.Owner          = In.Owner;
        Out.HeaderPath     = In.HeaderPath;
        Out.Kind           = EParsedTypeKind::Enum;
        Out.bScopedEnum    = In.bScopedEnum;
        Out.ReflectionType = "Enum";
        Out.EnumValues     = In.Values;
        // Owner 保持空:namespace 级 scoped enum。GenerateEnumHeader 对
        // `bScopedEnum && Owner.empty()` 用 QualifiedName 生成真实注册
        // (如 mession::script::EScriptLanguage::Value),不再 no-op。
        return Out;
    }

    /**
     * @brief 生成一个 enum 的 .mgenerated.h 内容。
     * @param InEnum IR 枚举
     * @return 头文件全文(经 ToLegacyEnum 转成 legacy 视图后走原有 GenerateHeader)
     */
    MString MCodeGenerator::GenerateEnumHeaderFromIR(const mession::headercodegen::SParsedEnum& InEnum) const {
        return GenerateHeader(ToLegacyEnum(InEnum), {});
    }

    /**
     * @brief 生成一个 enum 的 .mgenerated.cpp 内容。
     * @param InEnum IR 枚举
     * @return 源文件全文(经 ToLegacyEnum 转成 legacy 视图后走原有 GenerateSource)
     */
    MString MCodeGenerator::GenerateEnumSourceFromIR(const mession::headercodegen::SParsedEnum& InEnum) const {
        return GenerateSource(ToLegacyEnum(InEnum));
    }

} // namespace MHeaderTool