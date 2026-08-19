#include "AST/ClangToolRunner.h"
#include "AST/ASTDumpAction.h"

#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"

#include <filesystem>

namespace mession::headercodegen {

    namespace {

        // 递归收集 InRoot 下所有源文件(.h/.hpp/.cpp/.cc)。CDB fallback 路径下用 —
        // ClangTool 需要明确文件列表。
        void CollectHeaders(const fs::path& InRoot, TVector<MString>& OutFiles) {
            if (!fs::exists(InRoot)) {
                return;
            }
            for (auto It = fs::recursive_directory_iterator(InRoot, fs::directory_options::skip_permission_denied); It != fs::recursive_directory_iterator(); ++It) {
                if (It->is_directory()) {
                    continue;
                }
                const fs::path& P   = It->path();
                const MString   Ext = P.extension().string();
                if (Ext == ".h" || Ext == ".hpp" || Ext == ".cpp" || Ext == ".cc") {
                    OutFiles.push_back(P.generic_string());
                }
            }
        }

    } // namespace

    SParseIR MClangToolRunner::RunDump(const MHeaderTool::SOptions& InOptions) {
        SParseIR IR;

        // 加载 compile_commands.json — ClangTool 入口
        MString          ErrMsg;
        auto             CDB = clang::tooling::CompilationDatabase::loadFromDirectory(InOptions.SourceRoot.generic_string(), ErrMsg);
        TVector<MString> SourceFiles;
        if (CDB) {
            for (const auto& File : CDB->getAllFiles()) {
                SourceFiles.push_back(File);
            }
        } else {
            // fallback: 构造固定 -fsyntax-only 命令行,递归扫描 SourceRoot 下所有头文件
            // -I<SourceRoot> 让相对 include("Common/Runtime/MLib.h" 形式)可被解析,
            // 否则 Clang 静默丢弃 TU,业务类型不会进入 IR。
            const MString IncludeArg = MString("-I") + fs::absolute(InOptions.SourceRoot).generic_string();
            CDB                      = std::make_unique<clang::tooling::FixedCompilationDatabase>(InOptions.SourceRoot.generic_string(), TVector<MString>{"-fsyntax-only", IncludeArg});
            CollectHeaders(InOptions.SourceRoot, SourceFiles);
        }

        if (SourceFiles.empty()) {
            // 无可解析文件 — 直接返回空 IR(测试将由 EXPECT_TRUE 失败)
            return IR;
        }

        clang::tooling::ClangTool Tool(*CDB, SourceFiles);
        MASTDumpAction::SetIR(&IR);
        Tool.run(clang::tooling::newFrontendActionFactory<MASTDumpAction>().get());

        return IR;
    }

} // namespace mession::headercodegen