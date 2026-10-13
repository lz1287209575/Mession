/**
 * @file ASTDumpTest.cpp
 * @brief 冒烟测试:对 Source/ 跑一遍 Clang AST dump,验证能解析出记录。
 *
 * @detail 只断言 "IR.Records 非空";为了让产物可以逐条 grep 核对,会把每条记录的 Name
 *         逐行打印。Options.SourceRoot 用相对路径 "Source",因此运行期 CWD 必须是仓库根。
 */
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

        // 逐条打印 Record 的 Name,便于把输出当产物 grep 核对。
        // NOTE: 这条 dump 路径刻意不做反射宏过滤(过滤在 ASTPipeline/ASTReflectionVisitor
        // 阶段),所以 Records 数会明显偏多 —— 因此只断言非空,不断言具体条数。
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