#include "AST/ASTPipeline.h"
#include "AST/ASTReflectionVisitor.h"

#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"

#include <filesystem>

namespace mession::headercodegen
{

namespace
{

// 递归收集 InRoot 下所有源文件(.h/.hpp/.cpp/.cc) — fallback 路径下用。
// 与 AST/ClangToolRunner.cpp 里的同名逻辑保持一致。
void CollectHeaders(const fs::path& InRoot, TVector<MString>& OutFiles)
{
    if (!fs::exists(InRoot))
    {
        return;
    }
    for (auto It = fs::recursive_directory_iterator(InRoot, fs::directory_options::skip_permission_denied);
         It != fs::recursive_directory_iterator();
         ++It)
    {
        if (It->is_directory())
        {
            continue;
        }
        const fs::path& P   = It->path();
        const MString   Ext = P.extension().string();
        if (Ext == ".h" || Ext == ".hpp" || Ext == ".cpp" || Ext == ".cc")
        {
            OutFiles.push_back(P.generic_string());
        }
    }
}

}  // namespace

class MASTReflectionConsumer : public clang::ASTConsumer
{
public:
    MASTReflectionConsumer(clang::ASTContext& Ctx, SParseIR& IR)
        : Visitor(Ctx, IR) {}

    void HandleTranslationUnit(clang::ASTContext& Ctx) override
    {
        Visitor.TraverseDecl(Ctx.getTranslationUnitDecl());
    }

private:
    MASTReflectionVisitor Visitor;
};

class MASTReflectionAction : public clang::ASTFrontendAction
{
public:
    explicit MASTReflectionAction(SParseIR& IR) : IRRef(IR) {}

    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
        clang::CompilerInstance& CI, llvm::StringRef /*File*/) override
    {
        CI.getDiagnostics().setClient(new clang::IgnoringDiagConsumer());
        return std::make_unique<MASTReflectionConsumer>(CI.getASTContext(), IRRef);
    }

private:
    SParseIR& IRRef;
};

class MFactory : public clang::tooling::FrontendActionFactory
{
public:
    explicit MFactory(SParseIR& IR) : IRRef(IR) {}

    std::unique_ptr<clang::FrontendAction> create() override
    {
        return std::make_unique<MASTReflectionAction>(IRRef);
    }

private:
    SParseIR& IRRef;
};

SParseIR MASTPipeline::Run(const SOptions& InOptions)
{
    SParseIR IR;

    // 加载 compile_commands.json — ClangTool 入口
    // Brief 使用 loadFromJSONFile;LLVM 17 实际 API 是 loadFromDirectory
    // (按 SourceRoot 上溯查找 compile_commands.json)。
    const fs::path SourceRootAbs = fs::absolute(InOptions.SourceRoot);
    MString        ErrMsg;
    auto           CDB = clang::tooling::CompilationDatabase::loadFromDirectory(
        SourceRootAbs.generic_string(),
        ErrMsg);

    TVector<MString> SourceFiles;
    if (CDB)
    {
        for (const auto& File : CDB->getAllFiles())
        {
            SourceFiles.push_back(File);
        }
    }
    else
    {
        // fallback: 构造固定 -fsyntax-only 命令行
        // Brief 原版只放 `-fsyntax-only`,没有 -I 也没有实际源文件列表,
        // 直接把目录当 source 喂给 ClangTool 会报 "expected exactly one
        // compiler job"。补 -I<SourceRoot> 让相对 include 可解析,再递归
        // 收集 .h/.cpp 文件喂给 ClangTool,与 MClangToolRunner 行为对齐。
        const MString IncludeArg = MString("-I") + SourceRootAbs.generic_string();
        CDB = std::make_unique<clang::tooling::FixedCompilationDatabase>(
            SourceRootAbs.generic_string(),
            TVector<MString>{ "-fsyntax-only", IncludeArg });
        CollectHeaders(SourceRootAbs, SourceFiles);
    }

    if (SourceFiles.empty())
    {
        // 无可解析文件 — 直接返回空 IR(测试将由 EXPECT_TRUE 失败)
        return IR;
    }

    clang::tooling::ClangTool Tool(*CDB, SourceFiles);

    MFactory Factory(IR);
    Tool.run(&Factory);

    return IR;
}

}  // namespace mession::headercodegen
