#include "AST/ASTDumpAction.h"
#include "AST/ClangToolRunner.h"
#include "Core/Types.h"
#include "Common/Runtime/Log/Tests/TestHarness.h"

#include <cstdio>

namespace mession::headercodegen {

    TEST_CASE(ASTDump_RunOnSourceRoot) {
        SOptions Options;
        Options.SourceRoot = std::filesystem::absolute("Source").generic_string();
        Options.bVerbose   = false;

        MASTDumpAction::SetIR(nullptr);

        MClangToolRunner Runner;
        SParseIR         IR = Runner.RunDump(Options);

        std::printf("ASTDump found %zu records, %zu functions, %zu enums\n", IR.Records.size(), IR.Functions.size(), IR.Enums.size());

        EXPECT_TRUE(IR.Records.size() > 0);
    }

} // namespace mession::headercodegen

int main() {
    std::printf("Running ASTDumpTest\n");
    mession::headercodegen::Test_ASTDump_RunOnSourceRoot();
    RUN_TESTS();
    return 0;
}