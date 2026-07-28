#include <iostream>
#include <chrono>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <set>
#include <cctype>
#include <stdexcept>
#include <map>

#include "Core/Types.h"
#include "Common/Runtime/MLib.h"
#include "Util/FileUtil.h"
#include "Util/StringUtil.h"
#include "Parsing/HeaderScanner.h"
#include "Parsing/ClassParser.h"
#include "Parsing/PropertyParser.h"
#include "Parsing/FunctionParser.h"
#include "Parsing/EnumParser.h"
#include "Parsing/MacroExpander.h"
#include "Cache/BuildCache.h"
#include "Cache/CacheReader.h"
#include "Cache/CacheWriter.h"
#include "Cache/IncrementalDriver.h"
#include "Generation/CodeGenerator.h"
#include "Generation/ManifestGenerators.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace
{
namespace fs = std::filesystem;

namespace MHT = MHeaderTool;

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

MHT::SOptions ParseArgs(int argc, char** argv)
{
    MHT::SOptions options;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h")
        {
            PrintUsage(argv[0]);
            exit(0);
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
            exit(1);
        }
    }

    return options;
}

std::string GetMHeaderToolVersion()
{
    return "2.0.0-refactored";
}

}  // namespace

// P4 — spec 2026-07-28 §B: scan every header in `FileContents` for
// namespace-scope `MFUNCTION(Async)` free functions. Reject any free
// function that carries a transport tag (ServerCall/ClientCall/RPC/
// NetServer/NetClient/Client) — those belong on class methods, not on
// free functions. The error message cites the relevant specs so the
// user can navigate to the rule.
std::vector<MHT::SFreeAsyncFunc> ProcessFreeFunctions(
    const std::map<fs::path, std::string>& FileContents)
{
    std::vector<MHT::SFreeAsyncFunc> Result;

    // Transport tags that must NOT appear on a free function (spec 2026-07-24 §6.1
    // + 2026-07-28 §B — transport is a class-method concept).
    const TSet<std::string> TransportTags = {
        "ServerCall", "ClientCall", "RPC", "NetServer", "NetClient", "Client"
    };

    const std::string Needle = "MFUNCTION(";

    for (const auto& [HeaderPath, Contents] : FileContents)
    {
        // Skip headers that have no reflection markers — mirrors main()'s
        // `HeaderScanner::HasReflectionMarkers` short-circuit.
        if (Contents.find("MFUNCTION") == std::string::npos)
        {
            continue;
        }

        // v1 simplification (spec §6 risk register): mask only class/struct
        // declaration bodies so namespace blocks remain visible. We tokenize
        // the header and look for `class` or `struct` keywords at token
        // boundaries; on match, we find the next `{` and mask everything
        // between the keyword and the matching `}`. Known limitation:
        // `class`/`struct` literals inside `// ...` or `/* ... */` comments
        // will be treated as keywords (P4 fixture files avoid that).
        std::string Masked = Contents;
        size_t Index = 0;
        int ClassDepth = 0;
        while (Index < Masked.size())
        {
            if (ClassDepth > 0)
            {
                const char Char = Masked[Index];
                if (Char == '{')
                {
                    ++ClassDepth;
                }
                else if (Char == '}')
                {
                    --ClassDepth;
                }
                else
                {
                    Masked[Index] = ' ';
                }
                ++Index;
            }
            else
            {
                const bool AtTokenBoundary = (Index == 0)
                    || std::isspace(static_cast<unsigned char>(Masked[Index - 1]))
                    || Masked[Index - 1] == ';'
                    || Masked[Index - 1] == '{'
                    || Masked[Index - 1] == '}';
                const bool IsClass = AtTokenBoundary
                    && (Masked.compare(Index, 5, "class") == 0)
                    && (Index + 5 < Masked.size())
                    && std::isspace(static_cast<unsigned char>(Masked[Index + 5]));
                const bool IsStruct = AtTokenBoundary
                    && !IsClass
                    && (Masked.compare(Index, 6, "struct") == 0)
                    && (Index + 6 < Masked.size())
                    && std::isspace(static_cast<unsigned char>(Masked[Index + 6]));
                if (IsClass || IsStruct)
                {
                    const size_t KeywordLen = IsClass ? 5 : 6;
                    const size_t BracePos = Masked.find('{', Index + KeywordLen);
                    if (BracePos != std::string::npos)
                    {
                        // Mask keyword + declaration header up to '{' (keep '{' visible
                        // so the inner brace-tracking loop sees it).
                        for (size_t K = Index; K < BracePos; ++K)
                        {
                            Masked[K] = ' ';
                        }
                        ClassDepth = 1;
                        Index = BracePos + 1;
                        continue;
                    }
                }
                ++Index;
            }
        }

        size_t SearchPos = 0;
        while ((SearchPos = Masked.find(Needle, SearchPos)) != std::string::npos)
        {
            const size_t MacroOpen = Contents.find('(', SearchPos);
            const size_t MacroClose = (MacroOpen == std::string::npos)
                ? std::string::npos
                : MHT::FindMatching(Contents, MacroOpen, '(', ')');
            if ((MacroOpen == std::string::npos) || (MacroClose == std::string::npos))
            {
                SearchPos = SearchPos + Needle.size();
                continue;
            }

            const std::string MacroArgs =
                Contents.substr(MacroOpen + 1, MacroClose - MacroOpen - 1);

            // Tokenize MacroArgs on ',' and reject any transport tag with an
            // exact match. Substring matching is wrong (e.g. "ClientCall"
            // would be flagged as carrying "Client" — see F4 review).
            TVector<std::string> Tokens;
            {
                size_t TokenStart = 0;
                for (size_t K = 0; K <= MacroArgs.size(); ++K)
                {
                    if ((K == MacroArgs.size()) || (MacroArgs[K] == ','))
                    {
                        Tokens.push_back(MHT::Trim(MacroArgs.substr(TokenStart, K - TokenStart)));
                        TokenStart = K + 1;
                    }
                }
            }
            for (const std::string& Token : Tokens)
            {
                if (TransportTags.find(Token) != TransportTags.end())
                {
                    throw std::runtime_error(
                        "MHeaderTool: free function at " + HeaderPath.string() +
                        " carries transport tag '" + Token +
                        "' in MFUNCTION — spec 2026-07-24 §6.1 + 2026-07-28 §B "
                        "require transport on class methods only; "
                        "use plain `MFUNCTION(Async)` for free functions");
                }
            }

            // Only handle MFUNCTION(Async) — i.e. exactly one token, equal to
            // "Async". Anything else is irrelevant to free-function codegen.
            if ((Tokens.size() != 1) || (Tokens[0] != "Async"))
            {
                SearchPos = MacroClose + 1;
                continue;
            }

            // Parse the declaration that follows the macro: walk past
            // whitespace + return-type, find the '(' of the signature, then
            // the function name (last identifier before '('), and extract
            // the response type.
            size_t DeclStart = MacroClose + 1;
            while ((DeclStart < Contents.size()) &&
                   std::isspace(static_cast<unsigned char>(Contents[DeclStart])))
            {
                ++DeclStart;
            }
            const size_t SigOpen = Contents.find('(', DeclStart);
            if (SigOpen == std::string::npos)
            {
                SearchPos = MacroClose + 1;
                continue;
            }
            const size_t SigClose = MHT::FindMatching(Contents, SigOpen, '(', ')');
            if (SigClose == std::string::npos)
            {
                SearchPos = MacroClose + 1;
                continue;
            }
            // Walk backward from SigOpen over the return type to find the
            // identifier that is the function name (last token before '(').
            size_t NameEnd = SigOpen;
            while ((NameEnd > DeclStart) &&
                   std::isspace(static_cast<unsigned char>(Contents[NameEnd - 1])))
            {
                --NameEnd;
            }
            size_t NameStart = NameEnd;
            while ((NameStart > DeclStart) &&
                   (std::isalnum(static_cast<unsigned char>(Contents[NameStart - 1])) ||
                    (Contents[NameStart - 1] == '_')))
            {
                --NameStart;
            }
            if (NameStart == NameEnd)
            {
                SearchPos = MacroClose + 1;
                continue;
            }
            const std::string FuncName = Contents.substr(NameStart, NameEnd - NameStart);

            // Return type = everything from DeclStart up to the start of FuncName.
            const std::string ReturnType =
                MHT::Trim(Contents.substr(DeclStart, NameStart - DeclStart));

            // Response type = unwrap SFutureResult<T> to T. For now a local
            // string-stripping version; Task 3 Step 2 introduces a shared
            // helper in CodeGenerator.h that this code can switch to.
            std::string ResponseType = ReturnType;
            const std::string FutureResultPrefix = "SFutureResult<";
            if ((ResponseType.rfind(FutureResultPrefix, 0) == 0) &&
                (!ResponseType.empty()) &&
                (ResponseType.back() == '>'))
            {
                ResponseType = ResponseType.substr(
                    FutureResultPrefix.size(), ResponseType.size() - FutureResultPrefix.size() - 1);
            }

            MHT::SFreeAsyncFunc Func;
            Func.HeaderPath = HeaderPath;
            Func.Name = FuncName;
            Func.ResponseType = MHT::Trim(ResponseType);
            Func.AsyncBody = "";  // free functions are declaration-only in P4
            Result.push_back(std::move(Func));

            SearchPos = MacroClose + 1;
        }
    }

    return Result;
}

// 简化的主函数
int main(int argc, char** argv)
{
    auto startTime = std::chrono::high_resolution_clock::now();
    int exitCode = 1;

#ifdef _WIN32
    HANDLE mutex = nullptr;
    mutex = CreateMutexA(nullptr, FALSE, "MHeaderTool_SingleInstance");
    if (mutex == nullptr)
    {
        std::cerr << "Failed to create mutex\n";
        return 1;
    }
    DWORD waitResult = WaitForSingleObject(mutex, 60000);
    if (waitResult != WAIT_OBJECT_0)
    {
        CloseHandle(mutex);
        return 0;
    }
#endif

    MHT::SOptions options = MHT::SOptions();
    bool benchmark = false;

    try
    {
        options = ParseArgs(argc, argv);
        benchmark = options.bBenchmark;

        if (options.bVerbose)
        {
            std::cerr << "MHeaderTool v" << GetMHeaderToolVersion() << "\n";
            std::cerr << "Source root: " << options.SourceRoot << "\n";
            std::cerr << "Output dir: " << options.OutputDir << "\n";
        }

        MHT::CreateDirectory(options.CacheDir);
        MHT::CreateDirectory(options.OutputDir);

        // 增量编译检查
        MHT::SIncrementalDecision decision;
        bool shouldSkip = false;
        bool bIncrementalMode = false;
        if (options.bIncremental && !options.bForceFull)
        {
            MHT::HeaderScanner scanner(options);
            auto headers = scanner.ScanHeaders();
            MHT::IncrementalDriver driver(options);
            decision = driver.Decide(headers);

            if (decision.Action == MHT::EIncrementalAction::Skip)
            {
                std::cout << "MHeaderTool: Incremental cache hit, nothing to do.\n";
                shouldSkip = true;
            }
            else if (decision.Action == MHT::EIncrementalAction::Regenerate)
            {
                bIncrementalMode = true;
                if (decision.AffectedTypes.empty())
                {
                    // 即使没有文件变化，也要确保所有输出文件存在
                    std::cout << "MHeaderTool: Incremental cache hit, all types unchanged.\n";
                }
                else
                {
                    std::cout << "MHeaderTool: Incremental build, " << decision.AffectedTypes.size()
                              << " types need regeneration.\n";
                }
            }
        }

        if (!shouldSkip)
        {
            // 扫描所有 header 文件
            MHT::HeaderScanner scanner(options);
            auto headers = scanner.ScanHeaders();

            // 读取所有文件内容
            std::map<fs::path, std::string> fileContents;
            std::cerr << "DEBUG: Total headers found: " << headers.size() << "\n";
            for (const auto& header : headers)
            {
                fileContents[header] = MHT::ReadFile(header);
            }
            std::cerr << "DEBUG: fileContents loaded: " << fileContents.size() << "\n";

            // 解析所有类型（无论是否增量模式）
            std::vector<MHT::SParsedClass> allClasses;
            std::map<std::string, fs::path> typeToHeader;  // 用于增量模式判断

            for (const auto& [header, contents] : fileContents)
            {
                std::cerr << "DEBUG: Processing header: " << header.filename().string() << " contents.size=" << contents.size() << "\n";
                if (!MHT::HeaderScanner::HasReflectionMarkers(contents))
                {
                    std::cerr << "DEBUG: Header has no reflection markers, skipping\n";
                    continue;
                }

                MHT::ClassParser classParser;
                auto regions = classParser.ParseClassRegions(contents);
                for (const auto& region : regions)
                {
                    auto parsed = classParser.ParseClass(header, contents, region);
                    if (parsed)
                    {
                        allClasses.push_back(std::move(*parsed));
                    }
                }

                MHT::EnumParser enumParser;
                auto enums = enumParser.ParseEnumsInHeader(header, contents);
                for (auto& e : enums)
                {
                    allClasses.push_back(std::move(e));
                }
            }

            // 构建类型到头文件的映射（用于增量模式）
            for (const auto& cls : allClasses)
            {
                typeToHeader[cls.Name] = cls.HeaderPath;
            }

            // 更新缓存（使用相对路径以确保跨运行一致性）
            MHT::SBuildCache newCache;
            newCache.MHeaderToolVersion = GetMHeaderToolVersion();
            newCache.CreatedAt = std::chrono::system_clock::now();
            for (const auto& cls : allClasses)
            {
                MHT::STypeCacheEntry entry;
                entry.TypeName = cls.Name;
                // 转换为相对路径存储
                entry.HeaderPath = MHT::MakeRelativePath(cls.HeaderPath, options.SourceRoot);
                entry.HeaderFingerprint = scanner.ComputeFingerprint(cls.HeaderPath);
                newCache.TypeEntries.push_back(std::move(entry));
            }
            newCache.BuildIndices();
            MHT::CacheWriter(options.CacheDir).Save(newCache);

            // 构建需要重新生成的类型集合
            std::set<std::string> typesToRegenerate;
            if (bIncrementalMode)
            {
                for (const auto& typeName : decision.AffectedTypes)
                {
                    typesToRegenerate.insert(typeName);
                }
            }



            // 构建类型名到其父类列表的映射
            std::map<std::string, std::vector<std::string>> classParents;
            for (const auto& cls : allClasses)
            {
                classParents[cls.Name] = cls.AllParentClasses;
            }

            // 递归检查是否继承自 MObject
            auto recursivelyInheritsMObject = [&](const std::string& typeName, auto&& self) -> bool
            {
                if (typeName == "MObject")
                {
                    return true;
                }
                auto it = classParents.find(typeName);
                if (it == classParents.end())
                {
                    // 找不到这个类在已解析的类型中——保守返回 false 让上层报错
                    return false;
                }
                for (const auto& parent : it->second)
                {
                    if (self(parent, self))
                    {
                        return true;
                    }
                }
                return false;
            };

            // 过滤出需要生成的类型（跳过不继承自 MObject 的类，如组件等）
            std::vector<MHT::SParsedClass> classesToGenerate;
            std::cerr << "DEBUG: allClasses has " << allClasses.size() << " types\n";
            for (const auto& cls : allClasses)
            {
                // 递归检查是否继承自 MObject
                bool inheritsMObject = recursivelyInheritsMObject(cls.Name, recursivelyInheritsMObject);
                if (!inheritsMObject && cls.Kind == MHT::EParsedTypeKind::Class)
                {
                    std::cerr << "DEBUG: Skipping " << cls.Name << " (doesn't inherit MObject)\n";
                    continue;
                }
                if (cls.Name.find("World") != std::string::npos)
                {
                    std::cerr << "DEBUG: Found World type: " << cls.Name << " inheritsMObject=" << inheritsMObject << "\n";
                }
                classesToGenerate.push_back(cls);
            }
            std::cerr << "DEBUG: classesToGenerate has " << classesToGenerate.size() << " types\n";

            // P3: pre-group classes by source header path. GenerateHeader
            // needs ALL classes from the same header (not just the one being
            // generated) to emit Async Frame struct definitions before the
            // user header include — otherwise sibling per-class files
            // (e.g. SEchoServiceConfig.mgenerated.h) would not see the
            // Frame struct when the user header has inline async bodies.
            std::map<std::string, std::vector<MHT::SParsedClass>> classesByHeader;
            for (const auto& cls : classesToGenerate)
            {
                classesByHeader[cls.HeaderPath.string()].push_back(cls);
            }

            // 生成代码
            MHT::CodeGenerator codeGen(options);
            size_t generatedCount = 0;
            bool bSelectiveRegenerate = bIncrementalMode && !typesToRegenerate.empty();
            for (const auto& cls : classesToGenerate)
            {
                // 增量模式：跳过未变化的类型（仅当有选择性重建时才跳过）
                if (bSelectiveRegenerate && typesToRegenerate.find(cls.Name) == typesToRegenerate.end())
                {
                    continue;
                }

                // 生成头文件
                std::string headerCode = codeGen.GenerateHeader(
                    cls, classesByHeader[cls.HeaderPath.string()]);
                fs::path headerPath = options.OutputDir / (MHT::SanitizeIdentifier(cls.Name) + ".mgenerated.h");
                MHT::WriteFile(headerPath, headerCode);

                // P3 v1 inline-body design: emit Frame struct definitions
                // into a separate `<ClassName>_AsyncFrames.h` file. The user
                // header must #include this file before `class <ClassName>`
                // so inline async bodies can reference the Frame. Skip if
                // the class has no async functions (caller can omit the
                // include entirely).
                std::string asyncHeaderCode = codeGen.EmitAsyncFramesHeader(cls);
                if (!asyncHeaderCode.empty())
                {
                    fs::path asyncHeaderPath = options.OutputDir /
                        (MHT::SanitizeIdentifier(cls.Name) + "_AsyncFrames.h");
                    MHT::WriteFile(asyncHeaderPath, asyncHeaderCode);
                }

                // 生成源文件
                std::string sourceCode = codeGen.GenerateSource(cls);
                fs::path sourcePath = options.OutputDir / (MHT::SanitizeIdentifier(cls.Name) + ".mgenerated.cpp");
                MHT::WriteFile(sourcePath, sourceCode);

                ++generatedCount;
            }

            // 统计
            size_t classCount = 0, structCount = 0, enumCount = 0;
            for (const auto& cls : allClasses)
            {
                switch (cls.Kind)
                {
                case MHT::EParsedTypeKind::Class: ++classCount; break;
                case MHT::EParsedTypeKind::Struct: ++structCount; break;
                case MHT::EParsedTypeKind::Enum: ++enumCount; break;
                }
            }

            if (!shouldSkip)
            {
                if (generatedCount > 0)
                {
                    std::cout << "MHeaderTool generated " << generatedCount << " types ("
                              << (allClasses.size() - generatedCount) << " unchanged, skipped)\n";
                }
                else
                {
                    std::cout << "MHeaderTool discovered " << allClasses.size() << " reflected types:\n";
                    std::cout << "  Classes: " << classCount << ", Structs: " << structCount << ", Enums: " << enumCount << "\n";
                }
            }

            // 生成 CMake manifest
            if (!options.CMakeManifestPath.empty())
            {
                std::cerr << "DEBUG: Generating CMake manifest with " << classesToGenerate.size() << " types\n";
                MHT::ManifestGenerators manifestGen(options);
                std::string cmakeContent = manifestGen.GenerateCMakeManifest(classesToGenerate, {});
                std::cerr << "DEBUG: CMake manifest content length = " << cmakeContent.size() << "\n";
                MHT::WriteFile(options.CMakeManifestPath, cmakeContent);
                std::cerr << "DEBUG: CMake manifest written\n";
            }

            // 生成 Client manifest（FunctionId → Client_* 路由表）
            if (!options.ClientManifestPath.empty())
            {
                MHT::ManifestGenerators manifestGen(options);
                std::string clientContent = manifestGen.GenerateClientManifest(classesToGenerate);
                MHT::WriteFile(options.ClientManifestPath, clientContent);
                std::cerr << "DEBUG: ClientManifest written to " << options.ClientManifestPath.generic_string() << "\n";
            }
        }

        exitCode = 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "MHeaderTool error: " << ex.what() << "\n";
        exitCode = 1;
    }

#ifdef _WIN32
    if (mutex)
    {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
#endif

    if (exitCode == 0 && benchmark)
    {
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
        std::cerr << "Total time: " << duration.count() << "ms\n";
    }

    return exitCode;
}
