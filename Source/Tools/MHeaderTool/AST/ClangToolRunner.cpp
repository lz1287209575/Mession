/**
 * @file ClangToolRunner.cpp
 * @brief 加载编译数据库并跑 ClangTool,把 Clang 的 AST 遍历结果汇成 SParseIR。
 *
 * @detail 只在 AST dump 阶段使用;不做任何反射过滤,过滤由后续 ASTPipeline 负责。
 */
#include "AST/ClangToolRunner.h"
#include "AST/ASTDumpAction.h"

#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"

#include <filesystem>

namespace mession::headercodegen {

    namespace {

        /**
         * @brief 递归收集 InRoot 下所有源文件(.h/.hpp/.cpp/.cc)。
         * @param InRoot   扫描根目录;不存在时直接返回(OutFiles 不变)
         * @param OutFiles [out] 追加收集到的文件路径(不清理旧内容)
         *
         * @detail 仅在 compile_commands.json 缺失的 fallback 路径下使用 —— ClangTool 必须
         *         拿到显式文件列表。目录遍历带 skip_permission_denied,静默跳过无权限子目录;
         *         路径用 generic_string 统一成 '/' 分隔,保证跨平台可比对。
         */
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

        // ClangTool 的入口:先尝试加载 compile_commands.json(ErrMsg 仅用于诊断,失败不影响流程)
        MString          ErrMsg;
        auto             CDB = clang::tooling::CompilationDatabase::loadFromDirectory(InOptions.SourceRoot.generic_string(), ErrMsg);
        TVector<MString> SourceFiles;
        if (CDB) {
            for (const auto& File : CDB->getAllFiles()) {
                SourceFiles.push_back(File);
            }
        } else {
            // fallback:没有编译数据库时自造固定命令行 -fsyntax-only + 递归扫全部源文件。
            // -I<SourceRoot> 让仓库风格的相对 include("Common/Runtime/MLib.h")可解析;
            // 缺了它 Clang 会静默丢弃 TU,业务类型一个都进不了 IR。
            // 注意:这里同时用 SourceRoot 充当 FixedCompilationDatabase 的 BuildDir。
            const MString IncludeArg = MString("-I") + fs::absolute(InOptions.SourceRoot).generic_string();
            CDB                      = std::make_unique<clang::tooling::FixedCompilationDatabase>(InOptions.SourceRoot.generic_string(), TVector<MString>{"-fsyntax-only", IncludeArg});
            CollectHeaders(InOptions.SourceRoot, SourceFiles);
        }

        if (SourceFiles.empty()) {
            // 一个可解析文件都没有:直接返回空 IR。调用方(含测试)需自行判定空 IR 是否可接受。
            return IR;
        }

        clang::tooling::ClangTool Tool(*CDB, SourceFiles);
        // IR 通过进程级静态指针交给 MASTDumpAction(工厂无法携带外部引用);
        // Tool.run 同步跑完所有 TU,期间 &IR 始终有效。
        MASTDumpAction::SetIR(&IR);
        Tool.run(clang::tooling::newFrontendActionFactory<MASTDumpAction>().get());

        return IR;
    }

} // namespace mession::headercodegen