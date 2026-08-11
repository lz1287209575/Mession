#include <iostream>
#include <cctype>
#include <stdexcept>

#include "Core/Types.h"
#include "Common/Runtime/MLib.h"
#include "Util/FileUtil.h"
#include "Util/StringUtil.h"
#include "AST/ASTPipeline.h"
#include "Generation/CodeGenerator.h"

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
        std::string arg = argv[i];

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
// `MHeaderTool::` symbols (`SOptions`, `CodeGenerator`,
// `SanitizeIdentifier`, `WriteFile`, `CreateDirectory`) need their own
// reachability — `using namespace MHeaderTool;` is the minimum-friction
// way to honor the brief's "minimal qualification" intent. Renaming
// `CodeGenerator` to `MCodeGenerator` (per the brief's snippet) is out
// of scope for Task 8; that's an A3 cleanup item (Task 7 report §1).
int main(int argc, char** argv)
{
    using namespace mession::headercodegen;
    using namespace MHeaderTool;

    SOptions Options;
    if (!ParseArgs(argc, argv, Options)) return 1;

    CreateDirectory(Options.CacheDir);
    CreateDirectory(Options.OutputDir);

    SParseIR IR = MASTPipeline::Run(Options);

    CodeGenerator CodeGen(Options);
    for (const auto& Record : IR.Records)
    {
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
    }

    return 0;
}
