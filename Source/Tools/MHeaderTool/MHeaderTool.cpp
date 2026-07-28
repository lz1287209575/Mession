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

// Forward declaration — definition lives in MHeaderToolLib.cpp (P4
// extraction so tests can link this without colliding with main()).
std::vector<MHT::SFreeAsyncFunc> ProcessFreeFunctions(
    const std::map<fs::path, std::string>& FileContents);

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

            // P4: emit one `<Header>_FreeAsyncFrames.mgenerated.h` per header
            // that contains at least one `MFUNCTION(Async)` free function.
            // ProcessFreeFunctions was called above in Task 2 wiring;
            // for the actual integration in main() we re-scan here to keep
            // the per-header grouping symmetric with the class pass.
            TMap<std::string, std::vector<MHT::SFreeAsyncFunc>> FreeByHeader;
            {
                std::vector<MHT::SFreeAsyncFunc> AllFree = ProcessFreeFunctions(fileContents);
                for (auto& Func : AllFree)
                {
                    FreeByHeader[Func.HeaderPath.string()].push_back(std::move(Func));
                }
            }
            for (const auto& [HeaderStr, Funcs] : FreeByHeader)
            {
                std::string FreeCode = codeGen.EmitFreeAsyncFramesHeader(
                    Funcs, fs::path(HeaderStr));
                fs::path BaseName = fs::path(HeaderStr).stem();
                fs::path FreePath = options.OutputDir /
                    (MHT::SanitizeIdentifier(BaseName.string()) + "_FreeAsyncFrames.mgenerated.h");
                MHT::WriteFile(FreePath, FreeCode);
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
