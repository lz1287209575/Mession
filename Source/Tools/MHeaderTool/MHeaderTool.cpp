#include <iostream>
#include <cctype>
#include <stdexcept>

#include "Core/Types.h"
#include "Common/Runtime/MLib.h"
#include "Util/FileUtil.h"
#include "Util/StringUtil.h"
#include "AST/ASTPipeline.h"
#include "Generation/CodeGenerator.h"
#include "Generation/ManifestGenerators.h"
#include "Generation/LuaBindEmitter.h"

#include <map>

namespace
{
namespace fs = std::filesystem;

void PrintUsage(const char* progName)
{
    std::cerr << "Usage: " << progName << " [options]\n";
    std::cerr << "Options:\n";
    std::cerr << "  --source-root=<path>      Source root directory (default: Source)\n";
    std::cerr << "  --output-dir=<path>       Output directory (default: Build/Generated)\n";
    std::cerr << "  --cache-dir=<path>        Cache directory (default: Build/.mheadertool_cache)\n";
    std::cerr << "  --cmake-manifest=<path>   CMake manifest path\n";
    std::cerr << "  --validation-schema-out=<path>  Validation schema output path\n";
    std::cerr << "  --client-manifest=<path>  Client manifest output path (.cpp)\n";
    std::cerr << "  --verbose                 Verbose output\n";
    std::cerr << "  --incremental             Enable incremental build (default: true)\n";
    std::cerr << "  --force-full             Force full rebuild\n";
    std::cerr << "  --benchmark              Show timing statistics\n";
    std::cerr << "  --dry-run                Show what would be done without doing it\n";
    std::cerr << "  --jobs=<n>               Number of parallel jobs (default: auto)\n";
}

// A2 (Task 8) — main rewrite: return bool + take Options& so the brief's
// `if (!ParseArgs(...)) return 1;` style works. Old signature returned
// `MHT::SOptions` by value (legacy string-parser entry point).
bool ParseArgs(int argc, char** argv, MHeaderTool::SOptions& options)
{
    for (int i = 1; i < argc; ++i)
    {
        MString arg = argv[i];

        if (arg == "--help" || arg == "-h")
        {
            PrintUsage(argv[0]);
            std::exit(0);
        }
        else if (arg == "--verbose")
        {
            options.bVerbose = true;
        }
        else if (arg == "--incremental")
        {
            options.bIncremental = true;
        }
        else if (arg == "--force-full")
        {
            options.bForceFull = true;
        }
        else if (arg == "--benchmark")
        {
            options.bBenchmark = true;
        }
        else if (arg == "--dry-run")
        {
            options.bDryRun = true;
        }
        else if (arg.rfind("--source-root=", 0) == 0)
        {
            options.SourceRoot = arg.substr(14);
        }
        else if (arg.rfind("--output-dir=", 0) == 0)
        {
            options.OutputDir = arg.substr(13);
        }
        else if (arg.rfind("--cache-dir=", 0) == 0)
        {
            options.CacheDir = arg.substr(12);
        }
        else if (arg.rfind("--cmake-manifest=", 0) == 0)
        {
            options.CMakeManifestPath = arg.substr(17);
        }
        else if (arg.rfind("--validation-schema-out=", 0) == 0)
        {
            options.ValidationSchemaPath = arg.substr(24);
        }
        else if (arg.rfind("--client-manifest=", 0) == 0)
        {
            options.ClientManifestPath = arg.substr(18);
        }
        else if (arg.rfind("--jobs=", 0) == 0)
        {
            options.NumThreads = std::stoi(arg.substr(7));
        }
        else
        {
            std::cerr << "Unknown option: " << arg << "\n";
            PrintUsage(argv[0]);
            return false;
        }
    }

    return true;
}

}  // namespace

// A2 (Task 8) — main rewritten to the AST path. Brief uses
// `using namespace mession::headercodegen;` so unqualified
// `MASTPipeline` / `SParseIR` / `SParsedRecord` resolve. The remaining
// `MHeaderTool::` symbols (`SOptions`, `MCodeGenerator`,
// `SanitizeIdentifier`, `WriteFile`, `CreateDirectory`) need their own
// reachability — `using namespace MHeaderTool;` is the minimum-friction
// way to honor the brief's "minimal qualification" intent. Renaming
// `MCodeGenerator` to `MCodeGenerator` (per the brief's snippet) is out
// of scope for Task 8; that's an A3 cleanup item (Task 7 report §1).
int main(int argc, char** argv)
{
    using namespace mession::headercodegen;
    using namespace MHeaderTool;

    SOptions Options;
    if (!ParseArgs(argc, argv, Options)) return 1;

    CreateDirectory(Options.CacheDir);
    CreateDirectory(Options.OutputDir);
    // ClangTool 解析时会 chdir 到 compile_commands 的 directory——相对 OutputDir
    // 会在错误 cwd 下写文件（ofstream 失败静默）。统一绝对化，任何调用方式都稳。
    Options.OutputDir = fs::absolute(Options.OutputDir);

    SParseIR IR = MASTPipeline::Run(Options);

    MCodeGenerator CodeGen(Options);
    TSet<MString> WrittenTypeNames;
    for (const auto& Record : IR.Records)
    {
        // 同名类型（不同 TU 的重复 Record / 同名不同类）只生成一次，
        // 避免后写空 Record 覆盖完整文件。
        if (WrittenTypeNames.find(Record.Name) != WrittenTypeNames.end()) continue;
        WrittenTypeNames.insert(Record.Name);

        const MString HeaderCode = CodeGen.GenerateHeaderFromIR(Record, IR.Records);
        const MString SourceCode = CodeGen.GenerateSourceFromIR(Record);
        WriteFile(Options.OutputDir / (SanitizeIdentifier(Record.Name) + ".mgenerated.h"),
            HeaderCode);
        WriteFile(Options.OutputDir / (SanitizeIdentifier(Record.Name) + ".mgenerated.cpp"),
            SourceCode);

        const MString AsyncHeader = CodeGen.EmitAsyncFramesHeaderFromIR(Record);
        if (!AsyncHeader.empty())
        {
            WriteFile(Options.OutputDir / (SanitizeIdentifier(Record.Name) + "_AsyncFrames.h"),
                AsyncHeader);
        }

        // await — TAwaitable 状态机 Frame（独立文件）
        const MString AwaitStateMachine = CodeGen.EmitAwaitStateMachineHeader(Record);
        if (!AwaitStateMachine.empty())
        {
            WriteFile(Options.OutputDir / (SanitizeIdentifier(Record.Name) + "_AwaitStateMachine.h"),
                AwaitStateMachine);
        }

    }

    // A2 follow-up (AST 重构 merge) — enum 生成（SParseIR::Enums → legacy shim）。
    for (const auto& Enum : IR.Enums)
    {
        if (WrittenTypeNames.find(Enum.Name) != WrittenTypeNames.end()) continue;
        WrittenTypeNames.insert(Enum.Name);

        const MString EnumHeader = CodeGen.GenerateEnumHeaderFromIR(Enum);
        const MString EnumSource = CodeGen.GenerateEnumSourceFromIR(Enum);
        WriteFile(Options.OutputDir / (SanitizeIdentifier(Enum.Name) + ".mgenerated.h"),
            EnumHeader);
        WriteFile(Options.OutputDir / (SanitizeIdentifier(Enum.Name) + ".mgenerated.cpp"),
            EnumSource);
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
        TSet<MString> SeenFreeFuncs;
        for (const auto& Fn : IR.FreeFunctions)
        {
            const MString DedupKey = Fn.HeaderPath.generic_string() + "::" + Fn.Name;
            if (SeenFreeFuncs.find(DedupKey) != SeenFreeFuncs.end()) continue;
            SeenFreeFuncs.insert(DedupKey);

            MString RespType = Fn.ReturnType.CanonicalName;
            const MString FuturePrefix = "SFutureResult<";
            if (RespType.rfind(FuturePrefix, 0) == 0 && !RespType.empty()
                && RespType.back() == '>')
            {
                RespType = RespType.substr(FuturePrefix.size(),
                    RespType.size() - FuturePrefix.size() - 1);
            }

            SFreeAsyncFunc Legacy;
            Legacy.HeaderPath   = Fn.HeaderPath;
            Legacy.Name         = Fn.Name;
            Legacy.ResponseType = RespType;
            Legacy.AsyncBody    = Fn.AsyncBody;
            FreeByHeader[Fn.HeaderPath].push_back(std::move(Legacy));
        }
        for (const auto& [HeaderStr, Funcs] : FreeByHeader)
        {
            const MString FreeCode = CodeGen.EmitFreeAsyncFramesHeader(Funcs, fs::path(HeaderStr));
            if (FreeCode.empty()) continue;
            const fs::path BaseName = fs::path(HeaderStr).stem();
            WriteFile(Options.OutputDir / (SanitizeIdentifier(BaseName.string()) + "_FreeAsyncFrames.mgenerated.h"),
                FreeCode);

        }
    }

    // await — 自由函数状态机 Frame（IR 原生分组——AwaitSites/LiveAcrossAwait
    // 只在 IR SParsedFunction 上，legacy SFreeAsyncFunc 不携带）
    {
        std::map<fs::path, TVector<mession::headercodegen::SParsedFunction>> AwaitFreeByHeader;
        for (const auto& Fn : IR.FreeFunctions)
        {
            AwaitFreeByHeader[Fn.HeaderPath].push_back(Fn);
        }
        for (const auto& [HeaderStr, Funcs] : AwaitFreeByHeader)
        {
            const MString AwaitFree = CodeGen.EmitFreeAwaitStateMachineHeader(Funcs, fs::path(HeaderStr));
            if (AwaitFree.empty()) continue;
            const fs::path BaseName = fs::path(HeaderStr).stem();
            WriteFile(Options.OutputDir / (SanitizeIdentifier(BaseName.string()) + "_FreeAwaitStateMachine.h"),
                AwaitFree);

        }
    }

    // A2 follow-up (AST 重构 merge) — CMake / Client manifest。旧 main 在字符串解析
    // 路径末尾生成这两个文件；AST 路径经 ToLegacyClasses 复用 ManifestGenerators。
    const TVector<SParsedClass> LegacyClasses = CodeGen.ToLegacyClasses(IR);
    if (!Options.CMakeManifestPath.empty())
    {
        ManifestGenerators ManifestGen(Options);
        WriteFile(Options.CMakeManifestPath,
            ManifestGen.GenerateCMakeManifest(LegacyClasses, {}));
    }
    if (!Options.ClientManifestPath.empty())
    {
        ManifestGenerators ManifestGen(Options);
        WriteFile(Options.ClientManifestPath,
            ManifestGen.GenerateClientManifest(LegacyClasses));
    }

    // LuaBind — emit <Class>.lua / <Class>.d.tl（await merge 接线；
    // main 侧 LuaModule.cpp 原为 "LuaBindEmitter not yet wired"）。
    LuaBindEmitter::Run(Options.OutputDir, LegacyClasses);

    return 0;
}
