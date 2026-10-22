/**
 * @file MHeaderTool.cpp
 * @brief MHeaderTool(mht)的可执行入口:解析参数 → 跑 AST 管线 → 落盘全部生成物。
 *
 * @detail 当前只有**一条**主路径:MASTPipeline::Run 产出 SParseIR,再由 MCodeGenerator 的 IR
 *         入口逐条记录写 .mgenerated.h/.cpp、AsyncFrames、AwaitStateMachine、AwaitImpl,
 *         最后复用 ManifestGenerators / ScriptBindEmitter / LuaStdlibHintEmitter 产出各类
 *         清单与 Lua 侧产物。旧的字符串解析路径已整体删除,本文件里残留的 "A2"/"Task N"
 *         注释只是历史出处说明,不再是待办。
 *
 *         顺序上有两处硬依赖:
 *           1. 所有输出路径在使用前统一 fs::absolute() —— ClangTool 解析期间会 chdir 到
 *              compile_commands 的 directory,相对路径会在错误的 cwd 下落盘,而且 ofstream
 *              打开失败是静默的;
 *           2. SetReflectedClassKinds 必须在生成任何记录**之前**调用,否则继承链的 SetParent
 *              注册会缺表(见 actor-member-framework §5)。
 *         清单类产物的路径为空时整块跳过 —— 空路径即"不要这个产物"。
 */
#include <cctype>
#include <iostream>
#include <stdexcept>

#include "AST/ASTPipeline.h"
#include "Cache/CacheReader.h"
#include "Cache/CacheWriter.h"
#include "Cache/IncrementalDriver.h"
#include "Core/Types.h"
#include "Generation/CodeGenerator.h"
#include "Generation/LuaStdlibHintEmitter.h"
#include "Generation/ManifestGenerators.h"
#include "Generation/ScriptBindEmitter.h"
#include "Util/FileUtil.h"
#include "Util/StringUtil.h"
#include "Common/Runtime/MLib.h"

#include <map>

namespace {
    namespace fs = std::filesystem;

    constexpr const char* CACHE_FILE_PREFIX = "@file:";

    /** @brief 收集会决定反射 IR 的输入头与 Async codegen 源。普通 .cpp 不在此列，
     *  因为它们的改动不应迫使反射器重新建 AST。 */
    TVector<fs::path> CollectReflectionInputs(const fs::path& SourceRoot) {
        TVector<fs::path> Result;
        std::error_code Ec;
        for (fs::recursive_directory_iterator It(SourceRoot, fs::directory_options::skip_permission_denied, Ec), End; It != End; It.increment(Ec)) {
            if (Ec || It->is_directory()) {
                Ec.clear();
                continue;
            }
            const MString Name = It->path().filename().string();
            const MString Ext  = It->path().extension().string();
            if (Ext == ".h" || Ext == ".hpp" || (Name.size() >= 10 && Name.compare(Name.size() - 10, 10, ".Async.cpp") == 0)) {
                Result.push_back(fs::absolute(It->path()));
            }
        }
        return Result;
    }

    /** @brief 缓存命中还需确认每个已反射类型的两个主生成物均存在。 */
    bool HasAllCachedOutputs(const MHeaderTool::SOptions& Options) {
        MHeaderTool::SBuildCache Cache;
        MHeaderTool::CacheReader Reader(Options.CacheDir);
        if (!Reader.Load(Cache)) {
            return false;
        }
        for (const auto& Entry : Cache.TypeEntries) {
            if (Entry.TypeName.rfind(CACHE_FILE_PREFIX, 0) == 0) {
                continue;
            }
            if (!MHeaderTool::FileExists(Entry.OutputHeader) || !MHeaderTool::FileExists(Entry.OutputSource)) {
                return false;
            }
        }
        return MHeaderTool::FileExists(Options.CMakeManifestPath);
    }

    /** @brief 保存当前输入的指纹；用 @file 条目覆盖“无反射类型的头也必须参与失效”的场景。 */
    void SaveIncrementalCache(const MHeaderTool::SOptions& Options, const mession::headercodegen::SParseIR& IR, const TVector<fs::path>& Inputs) {
        MHeaderTool::SBuildCache Cache;
        Cache.Version_  = MHeaderTool::SBuildCache::Version;
        Cache.CreatedAt = std::chrono::system_clock::now();
        MHeaderTool::IncrementalDriver Driver(Options);

        TSet<fs::path> Covered;
        auto AddEntry = [&](const MString& TypeName, const fs::path& Header) {
            const fs::path AbsoluteHeader = fs::absolute(Header);
            const fs::path RelativeHeader = Driver.MakeRelativePath(AbsoluteHeader);
            MHeaderTool::STypeCacheEntry Entry;
            Entry.TypeName          = TypeName;
            Entry.HeaderPath        = RelativeHeader;
            Entry.HeaderFingerprint = Driver.ComputeFingerprint(AbsoluteHeader);
            Entry.OutputHeader      = Options.OutputDir / (MHeaderTool::SanitizeIdentifier(TypeName) + ".mgenerated.h");
            Entry.OutputSource      = Options.OutputDir / (MHeaderTool::SanitizeIdentifier(TypeName) + ".mgenerated.cpp");
            Cache.TypeEntries.push_back(std::move(Entry));
            Covered.insert(RelativeHeader);
        };
        for (const auto& Record : IR.Records) {
            AddEntry(Record.Name, Record.HeaderPath);
        }
        for (const auto& Enum : IR.Enums) {
            AddEntry(Enum.Name, Enum.HeaderPath);
        }
        for (const fs::path& Input : Inputs) {
            const fs::path Relative = Driver.MakeRelativePath(Input);
            if (Covered.find(Relative) != Covered.end()) {
                continue;
            }
            MHeaderTool::STypeCacheEntry Entry;
            Entry.TypeName          = MString(CACHE_FILE_PREFIX) + Relative.generic_string();
            Entry.HeaderPath        = Relative;
            Entry.HeaderFingerprint = Driver.ComputeFingerprint(Input);
            Cache.TypeEntries.push_back(std::move(Entry));
        }
        Cache.BuildIndices();
        MHeaderTool::CacheWriter(Options.CacheDir).Save(Cache);
    }

    /**
     * @brief 把用法与全部选项打到 stderr。
     * @param progName argv[0],仅用于拼出进程名
     *
     * @detail NOTE: 帮助文本与 ParseArgs 里的选项表是**两处独立维护**的字符串,两边都要改;
     *         另外这里只描述选项,不代表选项真的被消费(见 ParseArgs 的 NOTE)。
     */
    void PrintUsage(const char* progName) {
        std::cerr << "Usage: " << progName << " [options]\n";
        std::cerr << "Options:\n";
        std::cerr << "  --source-root=<path>      Source root directory (default: Source)\n";
        std::cerr << "  --output-dir=<path>       Output directory (default: Build/Generated)\n";
        std::cerr << "  --cache-dir=<path>        Cache directory (default: Build/.mheadertool_cache)\n";
        std::cerr << "  --cmake-manifest=<path>   CMake manifest path\n";
        std::cerr << "  --validation-schema-out=<path>  Validation schema output path\n";
        std::cerr << "  --client-manifest=<path>  Client manifest output path (.cpp)\n";
        std::cerr << "  --client-downlink-manifest=<path>  Client downlink manifest output path\n";
        std::cerr << "  --client-downlink-header=<path>   Client downlink header output path\n";
        std::cerr << "  --member-rpc-manifest=<path>      Member RPC manifest output path (.cpp)\n";
        std::cerr << "  --lua-stdlib-hint-lua=<path>       Mession.lua output path (lua-language-server hint)\n";
        std::cerr << "  --lua-stdlib-hint-teal=<path>      Mession.d.tl output path (Teal type-checker hint)\n";
        std::cerr << "  --verbose                 Verbose output\n";
        std::cerr << "  --incremental             Enable incremental build (default: true)\n";
        std::cerr << "  --force-full             Force full rebuild\n";
        std::cerr << "  --benchmark              Show timing statistics\n";
        std::cerr << "  --dry-run                Show what would be done without doing it\n";
        std::cerr << "  --jobs=<n>               Number of parallel jobs (default: auto)\n";
    }

    /**
     * @brief 逐条解析命令行参数,就地填进 options。
     * @param argc,argv 原样透传
     * @param options   [in,out] 就地填充;未出现的选项保留调用方给的默认值
     * @return true 参数全部识别;false 遇到未知选项(此时已打印用法)
     *
     * @detail 只认 `--key=value` 形式和少数无值开关(--verbose/--incremental/--force-full/
     *         --benchmark/--dry-run);`--help`/`-h` 会直接 std::exit(0)。
     *         未知选项**立即**返回 false,后面的参数不再处理。
     *
     * @detail NOTE: 值部分用 arg.substr(固定长度) 切出来,改动选项名时必须同步改这个偏移量。
     *         `--jobs=` 的值经 std::stoi,非数字会抛 std::invalid_argument(未被捕获,进程直接终止)。
     *         NOTE: 以下选项目前**只写不读**,改它们不会产生任何行为差异 ——
     *         --verbose(bVerbose)、--incremental(bIncremental)、--benchmark(bBenchmark)、
     *         --dry-run(bDryRun)、--jobs=(NumThreads,线程数其实由 ASTPipeline 自己按核数算)、
     *         --validation-schema-out=(ValidationSchemaPath,而 GenerateValidationSchema 已无调用点)。
     *         --force-full 也只有 IncrementalDriver 会读,而整个 Cache/ 层当前没有调用方。
     */
    bool ParseArgs(int argc, char** argv, MHeaderTool::SOptions& options) {
        for (int i = 1; i < argc; ++i) {
            MString arg = argv[i];

            if (arg == "--help" || arg == "-h") {
                PrintUsage(argv[0]);
                std::exit(0);
            } else if (arg == "--verbose") {
                options.bVerbose = true;
            } else if (arg == "--incremental") {
                options.bIncremental = true;
            } else if (arg == "--force-full") {
                options.bForceFull = true;
            } else if (arg == "--benchmark") {
                options.bBenchmark = true;
            } else if (arg == "--dry-run") {
                options.bDryRun = true;
            } else if (arg.rfind("--source-root=", 0) == 0) {
                options.SourceRoot = arg.substr(14);
            } else if (arg.rfind("--output-dir=", 0) == 0) {
                options.OutputDir = arg.substr(13);
            } else if (arg.rfind("--cache-dir=", 0) == 0) {
                options.CacheDir = arg.substr(12);
            } else if (arg.rfind("--cmake-manifest=", 0) == 0) {
                options.CMakeManifestPath = arg.substr(17);
            } else if (arg.rfind("--validation-schema-out=", 0) == 0) {
                options.ValidationSchemaPath = arg.substr(24);
            } else if (arg.rfind("--client-manifest=", 0) == 0) {
                options.ClientManifestPath = arg.substr(18);
            } else if (arg.rfind("--client-downlink-manifest=", 0) == 0) {
                options.ClientDownlinkManifestPath = arg.substr(27);
            } else if (arg.rfind("--client-downlink-header=", 0) == 0) {
                options.ClientDownlinkHeaderPath = arg.substr(25);
            } else if (arg.rfind("--member-rpc-manifest=", 0) == 0) {
                options.MemberRpcManifestPath = arg.substr(22);
            } else if (arg.rfind("--lua-stdlib-hint-lua=", 0) == 0) {
                options.LuaStdlibHintLuaPath = arg.substr(22);
            } else if (arg.rfind("--lua-stdlib-hint-teal=", 0) == 0) {
                options.LuaStdlibHintTealPath = arg.substr(23);
            } else if (arg.rfind("--jobs=", 0) == 0) {
                options.NumThreads = std::stoi(arg.substr(7));
            } else {
                std::cerr << "Unknown option: " << arg << "\n";
                PrintUsage(argv[0]);
                return false;
            }
        }

        return true;
    }

} // namespace

/**
 * @brief 主流程:解析参数 → 跑 AST 管线 → 逐类落盘 → 清单 / 绑定产物。
 * @param argc,argv 命令行
 * @return 0 正常结束;1 参数解析失败
 *
 * @detail 两个 using 是为了让 `MASTPipeline` / `SParseIR`(mession::headercodegen)与
 *         `SOptions` / `MCodeGenerator` / `SanitizeIdentifier` / `WriteFile` / `CreateDirectory`
 *         (MHeaderTool)都能不加限定名直接用 —— 这两套符号目前各自分属两个命名空间,不做合并。
 *         落盘顺序:先按记录写(同名类型去重,避免后写的空记录覆盖完整文件),再按 enum 写,
 *         最后写 enum 注册引导与各类清单。enum 注册引导单独成 TU,用于绕开静态库链接时
 *         匿名静态对象被 GC 丢弃的问题。
 *
 * @detail NOTE: 原注释里 "Renaming `MCodeGenerator` to `MCodeGenerator`" 是自相矛盾的残留
 *         (等于把 X 重命名成 X),与现状也对不上,已按代码实际行为重写。
 */
int main(int argc, char** argv) {
    using namespace mession::headercodegen;
    using namespace MHeaderTool;

    SOptions Options;
    if (!ParseArgs(argc, argv, Options))
        return 1;

    CreateDirectory(Options.CacheDir);
    CreateDirectory(Options.OutputDir);
    // ClangTool 解析时会 chdir 到 compile_commands 的 directory——相对 OutputDir
    // 会在错误 cwd 下写文件（ofstream 失败静默）。统一绝对化，任何调用方式都稳。
    Options.OutputDir                  = fs::absolute(Options.OutputDir);
    Options.SourceRoot                 = fs::absolute(Options.SourceRoot);
    Options.CacheDir                   = fs::absolute(Options.CacheDir);
    Options.CMakeManifestPath          = fs::absolute(Options.CMakeManifestPath);
    Options.ClientManifestPath         = fs::absolute(Options.ClientManifestPath);
    Options.ClientDownlinkManifestPath = fs::absolute(Options.ClientDownlinkManifestPath);
    Options.ClientDownlinkHeaderPath   = fs::absolute(Options.ClientDownlinkHeaderPath);
    Options.MemberRpcManifestPath      = fs::absolute(Options.MemberRpcManifestPath);

    const TVector<fs::path> ReflectionInputs = CollectReflectionInputs(Options.SourceRoot);
    if (Options.bIncremental && !Options.bForceFull) {
        IncrementalDriver Driver(Options);
        const SIncrementalDecision Decision = Driver.Decide(ReflectionInputs);
        if (Decision.Action == EIncrementalAction::Skip && HasAllCachedOutputs(Options)) {
            if (Options.bVerbose) {
                std::cout << "MHeaderTool incremental cache hit: " << Decision.SkippedCount << " reflection inputs unchanged\n";
            }
            return 0;
        }
        if (Options.bVerbose) {
            std::cout << "MHeaderTool reflection cache miss: " << Decision.Reason << ", parsing " << ReflectionInputs.size() << " inputs\n";
        }
    }

    SParseIR IR = MASTPipeline::Run(Options);

    MCodeGenerator CodeGen(Options);

    // F0 actor-member-framework §5:注入"全反射类名 → 是否 MSTRUCT"表,
    // 供 BuildSetParentLine 为继承请求链(FPlayerRequestBase ← ...)生成
    // SetParent 注册(继承序列化先父后子,见 Class.cpp)。
    {
        TMap<MString, bool> ReflectedClassKinds;
        for (const auto& Record : IR.Records) {
            if (ReflectedClassKinds.find(Record.Name) == ReflectedClassKinds.end()) {
                ReflectedClassKinds[Record.Name] = (Record.Kind == mession::headercodegen::ERecordKind::Struct);
            }
        }
        CodeGen.SetReflectedClassKinds(ReflectedClassKinds);
    }

    TSet<MString> WrittenTypeNames;
    for (const auto& Record : IR.Records) {
        // 同名类型（不同 TU 的重复 Record / 同名不同类）只生成一次，
        // 避免后写空 Record 覆盖完整文件。
        if (WrittenTypeNames.find(Record.Name) != WrittenTypeNames.end())
            continue;
        WrittenTypeNames.insert(Record.Name);

        const MString HeaderCode = CodeGen.GenerateHeaderFromIR(Record, IR.Records);
        const MString SourceCode = CodeGen.GenerateSourceFromIR(Record);
        WriteFile(Options.OutputDir / (SanitizeIdentifier(Record.Name) + ".mgenerated.h"), HeaderCode);
        WriteFile(Options.OutputDir / (SanitizeIdentifier(Record.Name) + ".mgenerated.cpp"), SourceCode);

        const MString AsyncHeader = CodeGen.EmitAsyncFramesHeaderFromIR(Record);
        if (!AsyncHeader.empty()) {
            WriteFile(Options.OutputDir / (SanitizeIdentifier(Record.Name) + "_AsyncFrames.h"), AsyncHeader);
        }

        // await — TAwaitable 状态机 Frame（独立文件）
        const MString AwaitStateMachine = CodeGen.EmitAwaitStateMachineHeader(Record);
        if (!AwaitStateMachine.empty()) {
            WriteFile(Options.OutputDir / (SanitizeIdentifier(Record.Name) + "_AwaitStateMachine.h"), AwaitStateMachine);
        }

        // await — 状态机驱动函数实现（C# async 模型：业务逻辑体是 codegen 输入，
        // 编译版实现由此生成，含业务逻辑段）。生成物自包含（include 用户头 +
        // Frame 头），作为独立编译单元（cmake 编译链接，业务代码不 include）。
        {
            std::ostringstream AwaitImpl;
            for (const auto& F : Record.Functions) {
                if (!F.bIsAsync)
                    continue;
                const MString Impl = CodeGen.EmitAwaitFuncImpl(Record.Name, Record.HeaderPath, F);
                if (!Impl.empty()) {
                    AwaitImpl << Impl << "\n";
                }
            }
            const MString ImplAll = AwaitImpl.str();
            if (!ImplAll.empty()) {
                std::ostringstream Hdr;
                Hdr << "// Generated by MHeaderTool (await 状态机驱动 函数实现, C# async 模型)\n";
                Hdr << "// Source: " << Record.HeaderPath.string() << "\n\n";
                Hdr << "#include \"" << CodeGen.MakeIncludePathFromHeader(Record.HeaderPath) << "\"\n";
                Hdr << "#include \"" << SanitizeIdentifier(Record.Name) << "_AwaitStateMachine.h\"\n\n";
                WriteFile(Options.OutputDir / (SanitizeIdentifier(Record.Name) + "_AwaitImpl.mgenerated.cpp"), Hdr.str() + ImplAll);
            }
        }
    }

    // A2 follow-up (AST 重构 merge) — enum 生成（SParseIR::Enums → legacy shim）。
    for (const auto& Enum : IR.Enums) {
        if (WrittenTypeNames.find(Enum.Name) != WrittenTypeNames.end())
            continue;
        WrittenTypeNames.insert(Enum.Name);

        const MString EnumHeader = CodeGen.GenerateEnumHeaderFromIR(Enum);
        const MString EnumSource = CodeGen.GenerateEnumSourceFromIR(Enum);
        WriteFile(Options.OutputDir / (SanitizeIdentifier(Enum.Name) + ".mgenerated.h"), EnumHeader);
        WriteFile(Options.OutputDir / (SanitizeIdentifier(Enum.Name) + ".mgenerated.cpp"), EnumSource);
    }

    // 注册引导——namespace enum 的注册函数在 .mgenerated.h(inline),enum .cpp 的
    // 匿名静态对象在静态库链接时可能被 GC 丢弃(无外部引用)。这里生成集中的
    // MHeaderTool_RegisterAllEnums(),由反射层 FindEnum 惰性调用——被引用即拉取。
    {
        std::ostringstream Reg;
        Reg << "// Auto-generated by MHeaderTool — DO NOT EDIT\n";
        Reg << "// 注册引导:显式调用所有 namespace enum 的注册函数(解决静态库 GC)\n\n";
        Reg << "#include \"Common/Runtime/Reflect/Reflection.h\"\n";
        for (const auto& Enum : IR.Enums) {
            Reg << "#include \"" << SanitizeIdentifier(Enum.Name) << ".mgenerated.h\"\n";
        }
        Reg << "\nvoid MHeaderTool_RegisterAllEnums() {\n";
        for (const auto& Enum : IR.Enums) {
            Reg << "    (void)MHeaderTool_Generated_RegisterEnum_" << Enum.Name << "();\n";
        }
        Reg << "}\n";
        WriteFile(Options.OutputDir / "MEnumRegistration.mgenerated.cpp", Reg.str());
    }

    // P4 — free `MFUNCTION(Async)` Frame 生成（IR.FreeFunctions → legacy SFreeAsyncFunc）。
    // 旧字符串解析 main 对 ProcessFreeFunctions 结果调用 EmitFreeAsyncFramesHeader；
    // AST 路径从 SParseIR::FreeFunctions 转换出相同输入。
    //  - 同一头文件会被多个 TU include，同一函数（声明）会重复进 IR —— 按
    //    (HeaderPath, Name) 去重（对齐 ProcessFreeFunctions 的 SourceLine 去重意图）；
    //  - ResponseType 剥掉 `SFutureResult<` 前缀（对齐 ProcessFreeFunctions），
    //    否则 Frame 会生成 SFutureResult<SFutureResult<T>>。
    {
        TMap<fs::path, TVector<SFreeAsyncFunc>> FreeByHeader;
        TSet<MString>                           SeenFreeFuncs;
        for (const auto& Fn : IR.FreeFunctions) {
            const MString DedupKey = Fn.HeaderPath.generic_string() + "::" + Fn.Name;
            if (SeenFreeFuncs.find(DedupKey) != SeenFreeFuncs.end())
                continue;
            SeenFreeFuncs.insert(DedupKey);

            MString       RespType     = Fn.ReturnType.CanonicalName;
            const MString FuturePrefix = "SFutureResult<";
            if (RespType.rfind(FuturePrefix, 0) == 0 && !RespType.empty() && RespType.back() == '>') {
                RespType = RespType.substr(FuturePrefix.size(), RespType.size() - FuturePrefix.size() - 1);
            }

            SFreeAsyncFunc Legacy;
            Legacy.HeaderPath   = Fn.HeaderPath;
            Legacy.Name         = Fn.Name;
            Legacy.ResponseType = RespType;
            Legacy.AsyncBody    = Fn.AsyncBody;
            FreeByHeader[Fn.HeaderPath].push_back(std::move(Legacy));
        }
        for (const auto& [HeaderStr, Funcs] : FreeByHeader) {
            const MString FreeCode = CodeGen.EmitFreeAsyncFramesHeader(Funcs, fs::path(HeaderStr));
            if (FreeCode.empty())
                continue;
            const fs::path BaseName = fs::path(HeaderStr).stem();
            WriteFile(Options.OutputDir / (SanitizeIdentifier(BaseName.string()) + "_FreeAsyncFrames.mgenerated.h"), FreeCode);
        }
    }

    // await — 自由函数状态机 Frame（IR 原生分组——AwaitSites/LiveAcrossAwait
    // 只在 IR SParsedFunction 上，legacy SFreeAsyncFunc 不携带）
    {
        std::map<fs::path, TVector<mession::headercodegen::SParsedFunction>> AwaitFreeByHeader;
        for (const auto& Fn : IR.FreeFunctions) {
            AwaitFreeByHeader[Fn.HeaderPath].push_back(Fn);
        }
        for (const auto& [HeaderStr, Funcs] : AwaitFreeByHeader) {
            const MString AwaitFree = CodeGen.EmitFreeAwaitStateMachineHeader(Funcs, fs::path(HeaderStr));
            if (AwaitFree.empty())
                continue;
            const fs::path BaseName = fs::path(HeaderStr).stem();
            WriteFile(Options.OutputDir / (SanitizeIdentifier(BaseName.string()) + "_FreeAwaitStateMachine.h"), AwaitFree);

            // await — 自由函数状态机驱动实现（C# async 模型，自包含，独立编译单元）
            std::ostringstream FreeImpl;
            for (const auto& Fn : Funcs) {
                if (!Fn.bIsAsync)
                    continue;
                const MString Impl = CodeGen.EmitAwaitFuncImpl("Free", fs::path(HeaderStr), Fn);
                if (!Impl.empty())
                    FreeImpl << Impl << "\n";
            }
            const MString FreeImplAll = FreeImpl.str();
            if (!FreeImplAll.empty()) {
                std::ostringstream Hdr;
                Hdr << "// Generated by MHeaderTool (await 自由函数驱动实现, C# async 模型)\n";
                Hdr << "// Source: " << HeaderStr << "\n\n";
                Hdr << "#include \"" << CodeGen.MakeIncludePathFromHeader(fs::path(HeaderStr)) << "\"\n";
                Hdr << "#include \"" << SanitizeIdentifier(BaseName.string()) << "_FreeAwaitStateMachine.h\"\n\n";
                WriteFile(Options.OutputDir / (SanitizeIdentifier(BaseName.string()) + "_FreeAwaitImpl.mgenerated.cpp"), Hdr.str() + FreeImplAll);
            }
        }
    }

    // A2 follow-up(AST 重构 merge)—— CMake / Client manifest。旧 main 在字符串解析路径
    // 末尾生成这类清单;AST 路径经 ToLegacyClasses / ToLegacyClassesWithEnums 复用
    // ManifestGenerators。逐个路径判空,空路径的产物直接跳过。
    const TVector<SParsedClass> LegacyClasses = CodeGen.ToLegacyClasses(IR);
    if (!Options.CMakeManifestPath.empty()) {
        ManifestGenerators ManifestGen(Options);
        WriteFile(Options.CMakeManifestPath, ManifestGen.GenerateCMakeManifest(CodeGen.ToLegacyClassesWithEnums(IR), {}));
    }
    if (!Options.ClientManifestPath.empty()) {
        ManifestGenerators ManifestGen(Options);
        WriteFile(Options.ClientManifestPath, ManifestGen.GenerateClientManifest(LegacyClasses));
    }
    if (!Options.ClientDownlinkManifestPath.empty()) {
        ManifestGenerators ManifestGen(Options);
        WriteFile(Options.ClientDownlinkManifestPath, ManifestGen.GenerateClientDownlinkManifest(LegacyClasses));
    }
    if (!Options.ClientDownlinkHeaderPath.empty()) {
        ManifestGenerators ManifestGen(Options);
        WriteFile(Options.ClientDownlinkHeaderPath, ManifestGen.GenerateClientDownlinkHeader(LegacyClasses));
    }
    if (!Options.MemberRpcManifestPath.empty()) {
        ManifestGenerators ManifestGen(Options);
        WriteFile(Options.MemberRpcManifestPath, ManifestGen.GenerateMemberRpcManifest(LegacyClasses));
    }

    // ScriptBind — 为含 ScriptBind 函数的类生成 <Class>.lua / <Class>.d.tl
    // (输入是 legacy 视图 LegacyClasses;后续接 Python/TS 时加同名 Emitter 即可,共用 Transport 入口)。
    ScriptBindEmitter::Run(Options.OutputDir, LegacyClasses);

    // Lua stdlib 提示 — 生成 Mession.lua + Mession.d.tl 到 Build/Generated/
    // (注解源是 MLuaVector/Map/Log/Format/Rpc.cpp 里的 /// @lua-* 块)。
    if (!Options.LuaStdlibHintLuaPath.empty() && !Options.LuaStdlibHintTealPath.empty()) {
        LuaStdlibHintEmitter::Run(Options.SourceRoot, Options.LuaStdlibHintLuaPath, Options.LuaStdlibHintTealPath);
    }

    // 必须在全部生成物成功写出之后才更新缓存；中途失败时保留旧缓存，下次保守全量。
    if (Options.bIncremental && !Options.bDryRun) {
        SaveIncrementalCache(Options, IR, ReflectionInputs);
    }

    return 0;
}
