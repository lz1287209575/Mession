#include "AST/ASTDumpAction.h"
#include "AST/ClangToolRunner.h"
#include "Core/Types.h"
#include "Common/Runtime/Log/Tests/TestHarness.h"

#include <cstdio>

namespace mession::headercodegen {

    TEST_CASE(ASTDump_RunOnSourceRoot) {
        MHeaderTool::SOptions Options;
        Options.SourceRoot = std::filesystem::absolute("Source").generic_string();
        Options.bVerbose   = false;

        MASTDumpAction::SetIR(nullptr);

        MClangToolRunner Runner;
        SParseIR         IR = Runner.RunDump(Options);

        std::printf("ASTDump found %zu records, %zu functions, %zu enums\n", IR.Records.size(), IR.FreeFunctions.size(), IR.Enums.size());

        // Task 4 sign-off requires per-name verifiability — iterate Records and print
        // each Name so the artifact can be grep'd. Skeleton deliberately has no
        // reflection-macro filter (Task 6 will add it); the inflated count is expected.
        std::printf("---records---\n");
        for (const SParsedRecord& R : IR.Records) {
            std::printf("  %s\n", R.Name.c_str());
        }

        EXPECT_TRUE(IR.Records.size() > 0);
    }

} // namespace mession::headercodegen

int main() {
    std::printf("Running ASTDumpTest\n");
    mession::headercodegen::Test_ASTDump_RunOnSourceRoot();
    RUN_TESTS();
    return 0;
}