#include "AST/ASTPipeline.h"
#include "AST/ASTReflectionVisitor.h"

#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Lex/MacroArgs.h"

#include <filesystem>
#include <thread>

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

// 记录反射宏（MCLASS/MPROPERTY/MFUNCTION/MSTRUCT/MGENERATED_BODY）的展开：
// 预处理器在宏展开时回调，位置/参数来自源码事实——注释与字符串不会产生
// 宏展开事件，且没有字节窗口限制（超长 Meta 列表完整可取）。
class MReflectionMacroRecorder : public clang::PPCallbacks
{
public:
    MReflectionMacroRecorder(
        clang::SourceManager& SM, TVector<SMacroExpand>& Out)
        : SMRef(SM), OutRef(Out)
    {
    }

    void MacroExpands(const clang::Token& MacroNameTok,
                      const clang::MacroDefinition& /*MD*/,
                      clang::SourceRange Range,
                      const clang::MacroArgs* Args) override
    {
        const clang::IdentifierInfo* II = MacroNameTok.getIdentifierInfo();
        if (!II) return;
        const MString Name = II->getName().str();
        if (Name != "MFUNCTION" && Name != "MPROPERTY" && Name != "MCLASS"
            && Name != "MSTRUCT" && Name != "MGENERATED_BODY")
        {
            return;
        }
        // 注意：MacroExpands 的 Range 只覆盖宏名 token（不含参数）——参数
        // 文本必须从 MacroArgs 取（未展开 token 的源码拼写，保留原始格式）。
        // 无字节窗口：Meta=(...) 超长列表完整保留。
        MString ArgsText;
        clang::SourceLocation EndLoc = Range.getEnd();
        if (Args)
        {
            const unsigned NumArgs = Args->getNumMacroArguments();
            for (unsigned I = 0; I < NumArgs; ++I)
            {
                if (I > 0) ArgsText += ", ";
                const clang::Token* Toks = Args->getUnexpArgument(I);
                const unsigned Len = clang::MacroArgs::getArgLength(Toks);
                if (Len > 0)
                {
                    const clang::SourceLocation LastEnd = Toks[Len - 1].getEndLoc();
                    ArgsText += clang::Lexer::getSourceText(
                        clang::CharSourceRange::getCharRange(
                            Toks[0].getLocation(), LastEnd),
                        SMRef, clang::LangOptions()).str();
                    EndLoc = LastEnd;
                }
            }
        }

        SMacroExpand E;
        E.Name    = Name;
        E.Args    = std::move(ArgsText);
        E.EndLoc  = EndLoc;
        E.EndLine = SMRef.getSpellingLineNumber(EndLoc);
        OutRef.push_back(std::move(E));
    }

private:
    clang::SourceManager& SMRef;
    TVector<SMacroExpand>& OutRef;
};

class MASTReflectionConsumer : public clang::ASTConsumer
{
public:
    MASTReflectionConsumer(
        clang::ASTContext& Ctx, SParseIR& IR,
        const TVector<SMacroExpand>& MacroExpands)
        : Visitor(Ctx, IR, MacroExpands) {}

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
        // 每 TU 一份宏展开记录：注册 recorder 后再建 consumer（visitor 查记录）
        CI.getPreprocessor().addPPCallbacks(
            std::make_unique<MReflectionMacroRecorder>(CI.getSourceManager(), MacroExpands));
        return std::make_unique<MASTReflectionConsumer>(
            CI.getASTContext(), IRRef, MacroExpands);
    }

private:
    SParseIR& IRRef;
    TVector<SMacroExpand> MacroExpands;  // 每 TU（同一分片内 TU 串行解析）
};

// GCC 生成的 CMake PCH（cmake_pch.hxx.gch）clang libtooling 无法加载。
// 持有剥离了 `-include <pch>` 参数的编译命令，替换原 CDB 喂给 ClangTool。
class MPCHStrippedCompilationDatabase : public clang::tooling::CompilationDatabase
{
public:
    explicit MPCHStrippedCompilationDatabase(
        std::vector<clang::tooling::CompileCommand> InCommands)
        : Commands(std::move(InCommands))
    {
    }

    std::vector<clang::tooling::CompileCommand> getCompileCommands(
        llvm::StringRef File) const override
    {
        std::vector<clang::tooling::CompileCommand> Result;
        for (const auto& C : Commands)
        {
            if (C.Filename == File)
            {
                Result.push_back(C);
            }
        }
        return Result;
    }

    std::vector<std::string> getAllFiles() const override
    {
        std::vector<std::string> Result;
        for (const auto& C : Commands)
        {
            Result.push_back(C.Filename);
        }
        return Result;
    }

    std::vector<clang::tooling::CompileCommand> getAllCompileCommands() const override
    {
        return Commands;
    }

private:
    std::vector<clang::tooling::CompileCommand> Commands;
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

    // 加载 compile_commands.json — ClangTool 入口。
    // Brief 使用 loadFromJSONFile;LLVM 17 实际 API 是 loadFromDirectory。
    // loadFromDirectory 只在给定目录下找 compile_commands.json（不会上溯
    // 父目录），而本工程的 json 在 Build/ 下、SourceRoot 是 Source/——
    // 只试 SourceRoot 会加载失败，ClangTool 退到缺系统 include 的 fallback，
    // 导致 cstdint/fmt 找不到、类型被错误恢复（TVector<uint32> → int），
    // 诊断又被 IgnoringDiagConsumer 吞掉，产出静默错误的 .mgenerated。
    // 候选目录：SourceRoot 本身 / 项目根（SourceRoot 父）/ cwd / cwd/Build。
    const fs::path SourceRootAbs = fs::absolute(InOptions.SourceRoot);
    std::unique_ptr<clang::tooling::CompilationDatabase> CDB;
    {
        std::vector<fs::path> Candidates = {
            SourceRootAbs,
            SourceRootAbs.parent_path(),
            fs::current_path(),
            fs::current_path() / "Build",
            SourceRootAbs / ".." / "Build",
        };
        for (const auto& Cand : Candidates)
        {
            MString LoadErr;
            auto TryCDB = clang::tooling::CompilationDatabase::loadFromDirectory(
                Cand.generic_string(), LoadErr);
            if (TryCDB)
            {
                CDB = std::move(TryCDB);
                break;
            }
        }
    }

    // CMake 的 PCH（cmake_pch.hxx.gch）由 GCC 生成，clang libtooling 无法
    // 加载（每个 TU 报 "input is not a PCH file"）→ 解析失败、类型错误恢复。
    // 剥离 `-include <pch>` / `-Winvalid-pch` 等参数后重建 CDB。
    if (CDB)
    {
        std::vector<clang::tooling::CompileCommand> Stripped;
        for (const auto& Cmd : CDB->getAllCompileCommands())
        {
            clang::tooling::CompileCommand C = Cmd;
            std::vector<std::string> NewArgs;
            for (size_t I = 0; I < C.CommandLine.size(); ++I)
            {
                const std::string& A = C.CommandLine[I];
                if (A == "-include" || A == "-include-pch")
                {
                    if (I + 1 < C.CommandLine.size()) ++I;  // 跳过 PCH 路径
                    continue;
                }
                if (A.rfind("-includepch", 0) == 0) continue;
                if (A == "-Winvalid-pch") continue;
                NewArgs.push_back(A);
            }
            C.CommandLine = std::move(NewArgs);
            Stripped.push_back(std::move(C));
        }
        CDB = std::make_unique<MPCHStrippedCompilationDatabase>(std::move(Stripped));
    }

    TVector<MString> SourceFiles;
    if (CDB)
    {
        for (const auto& File : CDB->getAllFiles())
        {
            // 性能：compile_commands 含 160 个编译命令，其中大量 TU 不含
            // 业务反射类型（各目标的 PCH 生成 TU、Build/Generated 的
            // .mgenerated.cpp、Tests/、Tools/ 含 MHeaderTool 自身源码），
            // 逐个完整 clang 解析会显著拖慢生成。只保留 Source/ 与
            // Examples/ 下的源文件——反射类型要么在其中定义，要么被它们
            // include 传递带入。
            const MString F = fs::path(File).generic_string();
            if (F.find("/Build/") != MString::npos) continue;
            if (F.find("/Tests/") != MString::npos) continue;
            if (F.find("/Tools/") != MString::npos) continue;
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
        // fallback 直接解析 .h 文件：clang 对 .h 默认按 C 语言（struct 成员
        // 初始化在 C 里非法，如 `int X = 0;`）——必须显式 -x c++。
        CDB = std::make_unique<clang::tooling::FixedCompilationDatabase>(
            SourceRootAbs.generic_string(),
            TVector<MString>{ "-fsyntax-only", "-x", "c++", IncludeArg });
        CollectHeaders(SourceRootAbs, SourceFiles);
    }

    if (SourceFiles.empty())
    {
        // 无可解析文件 — 直接返回空 IR(测试将由 EXPECT_TRUE 失败)
        return IR;
    }

    // 性能：ClangTool::run 是单线程的，95 个 TU 串行完整解析约 2 分钟。
    // 本机 96 核——把 TU 分片、每片独立 ClangTool + 独立 SParseIR 并行跑，
    // 最后按 (QualifiedName, HeaderPath) 等键去重合并（各分片解析同一
    // 头文件会重复产出 Record/Enum/FreeFunctions）。
    const size_t NThreads = std::min<size_t>(
        SourceFiles.size(), static_cast<size_t>(std::thread::hardware_concurrency()));
    std::vector<TVector<MString>> Shards(NThreads);
    for (size_t I = 0; I < SourceFiles.size(); ++I)
    {
        Shards[I % NThreads].push_back(SourceFiles[I]);
    }

    std::vector<SParseIR> ShardIRs(NThreads);
    std::vector<std::thread> Workers;
    Workers.reserve(NThreads);
    for (size_t T = 0; T < NThreads; ++T)
    {
        Workers.emplace_back([&, T]()
        {
            clang::tooling::ClangTool Tool(*CDB, Shards[T]);
            MFactory Factory(ShardIRs[T]);
            Tool.run(&Factory);
        });
    }
    for (auto& W : Workers)
    {
        W.join();
    }

    // 合并各分片 IR（去重）
    for (const auto& Shard : ShardIRs)
    {
        for (const auto& Record : Shard.Records)
        {
            bool bDup = false;
            for (const auto& Existing : IR.Records)
            {
                if (Existing.QualifiedName == Record.QualifiedName
                    && Existing.HeaderPath == Record.HeaderPath)
                {
                    bDup = true;
                    break;
                }
            }
            if (!bDup) IR.Records.push_back(Record);
        }
        for (const auto& Enum : Shard.Enums)
        {
            bool bDup = false;
            for (const auto& Existing : IR.Enums)
            {
                if (Existing.Name == Enum.Name && Existing.HeaderPath == Enum.HeaderPath)
                {
                    bDup = true;
                    break;
                }
            }
            if (!bDup) IR.Enums.push_back(Enum);
        }
        for (const auto& Fn : Shard.FreeFunctions)
        {
            bool bDup = false;
            for (const auto& Existing : IR.FreeFunctions)
            {
                if (Existing.Name == Fn.Name && Existing.HeaderPath == Fn.HeaderPath)
                {
                    bDup = true;
                    break;
                }
            }
            if (!bDup) IR.FreeFunctions.push_back(Fn);
        }
    }

    return IR;
}

}  // namespace mession::headercodegen
