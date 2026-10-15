/**
 * @file ASTPipeline.cpp
 * @brief 组装反射专用的 ClangTool 管线:修补 CDB → 记录宏展开 → 按 TU 分片并行解析 → 合并去重。
 *
 * @detail 这是 MHeaderTool 的**解析主路径**(Tests 里的 MClangToolRunner 只是裸 dump,不做过滤):
 *   1. 找 compile_commands.json(多候选目录),剥掉 GCC 生成的 PCH 参数,再给不在数据库里的
 *      *.Async.cpp 造 fallback 编译命令;
 *   2. 只保留 Source/ 与 Protocol/Tests/ 下可能含反射类型的 TU(其余 TU 解析纯属浪费时间);
 *   3. 按 TU 分片,每片一个线程、一份独立 SParseIR 并行跑 ClangTool;
 *   4. 按 (QualifiedName, HeaderPath) 去重合并,同名函数优先保留"带函数体"的那份。
 *
 *   线程安全:各分片互不共享 SParseIR;共享的只有只读的 CDB(其查询接口都是 const)。
 */
#include "AST/ASTPipeline.h"
#include "AST/ASTReflectionVisitor.h"

#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Lex/MacroArgs.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"

#include <filesystem>
#include <thread>

namespace mession::headercodegen {

    namespace {

        /**
         * @brief 取当前 libclang 自带的 clang 资源头目录（含 stddef.h 等内置头）。
         *
         * @detail 为什么必须显式传给 libtooling:libtooling 不经过 clang driver,
         *         CompilerInvocation 的默认 -resource-dir 是按可执行文件路径推测的
         *         (<exe>/../../lib/clang/<major>)。clang 装在发行版标准位置时恰好命中,
         *         但装在非标准前缀(Debian 多版本并存、容器内 /tmp 组装树、conda 等)时
         *         探测必然失败 —— 缺 stddef.h 会让每个 TU 解析失败成 error 节点,
         *         函数体遍历不到,AwaitSites / LiveAcrossAwait 全空,await 生成物
         *         (*_AwaitImpl.mgenerated.cpp / *_AwaitStateMachine.h)一个都不产。
         *         该缺陷极隐蔽:AsyncFrames.h 仍正常生成(它不依赖函数体),只有 await
         *         产物静默缺失,直到 CMake 的 add_executable 报"找不到源文件"才暴露。
         *
         *         优先用 CMake 传进来的 MESSION_CLANG_RESOURCE_DIR(configure 期按
         *         ${LLVM_LIBRARY_DIRS}/clang/${LLVM_VERSION_MAJOR} 精确算出);未定义时
         *         退回空串,由 clang 自身按老逻辑探测(标准安装路径下仍能工作)。
         */
        MString GetClangResourceDirArg() {
#ifdef MESSION_CLANG_RESOURCE_DIR
            const MString Dir = MESSION_CLANG_RESOURCE_DIR;
            if (!Dir.empty()) {
                return "-resource-dir=" + Dir;
            }
#endif
            return {};
        }

        /**
         * @brief 递归收集 InRoot 下所有源文件(.h/.hpp/.cpp/.cc)。
         * @param InRoot   扫描根;不存在时直接返回
         * @param OutFiles [out] 追加收集结果(不清理旧内容)
         *
         * @detail 仅在 compile_commands.json 缺失的 fallback 路径下使用;与
         *         AST/ClangToolRunner.cpp 里的同名函数保持行为一致(两份实现是有意的重复,
         *         因为两条路径互不依赖对方的 TU)。
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

    /**
     * @brief 预处理器回调:把反射宏(MCLASS/MPROPERTY/MFUNCTION/MSTRUCT/MGENERATED_BODY)的展开记录下来。
     *
     * @detail 为什么走 PPCallbacks 而不是文本扫描:位置与参数都来自源码事实 —— 注释与字符串
     *         不会产生宏展开事件,也没有"字节窗口"限制,超长的 Meta=(...) 列表能完整取到。
     *         记录按 TU 内解析顺序追加,消费方(MASTReflectionVisitor)按倒序找"最近的、同文件的、
     *         在声明之前的"那次展开。
     */
    class MReflectionMacroRecorder : public clang::PPCallbacks {
        public:
        MReflectionMacroRecorder(clang::SourceManager& SM, TVector<SMacroExpand>& Out) : SMRef(SM), OutRef(Out) {
        }

        void MacroExpands(const clang::Token& MacroNameTok, const clang::MacroDefinition& /*MD*/, clang::SourceRange Range, const clang::MacroArgs* Args) override {
            const clang::IdentifierInfo* II = MacroNameTok.getIdentifierInfo();
            if (!II)
                return;
            const MString Name = II->getName().str();
            if (Name != "MFUNCTION" && Name != "MPROPERTY" && Name != "MCLASS" && Name != "MSTRUCT" && Name != "MGENERATED_BODY") {
                return;
            }
            // 注意：MacroExpands 的 Range 只覆盖宏名 token（不含参数）——参数
            // 文本必须从 MacroArgs 取（未展开 token 的源码拼写，保留原始格式）。
            // 无字节窗口：Meta=(...) 超长列表完整保留。
            MString               ArgsText;
            clang::SourceLocation EndLoc = Range.getEnd();
            if (Args) {
                const unsigned NumArgs = Args->getNumMacroArguments();
                for (unsigned I = 0; I < NumArgs; ++I) {
                    if (I > 0)
                        ArgsText += ", ";
                    const clang::Token* Toks = Args->getUnexpArgument(I);
                    const unsigned      Len  = clang::MacroArgs::getArgLength(Toks);
                    if (Len > 0) {
                        const clang::SourceLocation LastEnd = Toks[Len - 1].getEndLoc();
                        ArgsText += clang::Lexer::getSourceText(clang::CharSourceRange::getCharRange(Toks[0].getLocation(), LastEnd), SMRef, clang::LangOptions()).str();
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
        clang::SourceManager&  SMRef;
        TVector<SMacroExpand>& OutRef;
    };

    /**
     * @brief 单 TU 的 ASTConsumer:把 TU 交给 MASTReflectionVisitor 遍历。
     */
    class MASTReflectionConsumer : public clang::ASTConsumer {
        public:
        MASTReflectionConsumer(clang::ASTContext& Ctx, SParseIR& IR, const TVector<SMacroExpand>& MacroExpands) : Visitor(Ctx, IR, MacroExpands) {
        }

        void HandleTranslationUnit(clang::ASTContext& Ctx) override {
            Visitor.TraverseDecl(Ctx.getTranslationUnitDecl());
        }

        private:
        MASTReflectionVisitor Visitor;
    };

    /**
     * @brief 单个 TU 的前端动作:装 Ignoring 诊断 + 注册宏记录器 + 建 consumer。
     *
     * @detail MacroExpands 是**每 TU 一份**的成员(同一分片内 TU 串行解析),因此不需要加锁。
     */
    class MASTReflectionAction : public clang::ASTFrontendAction {
        public:
        explicit MASTReflectionAction(SParseIR& IR) : IRRef(IR) {
        }

        TUniquePtr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance& CI, llvm::StringRef /*File*/) override {
            CI.getDiagnostics().setClient(new clang::IgnoringDiagConsumer());
            // 每 TU 一份宏展开记录：注册 recorder 后再建 consumer（visitor 查记录）
            CI.getPreprocessor().addPPCallbacks(std::make_unique<MReflectionMacroRecorder>(CI.getSourceManager(), MacroExpands));
            return std::make_unique<MASTReflectionConsumer>(CI.getASTContext(), IRRef, MacroExpands);
        }

        private:
        SParseIR&             IRRef;
        TVector<SMacroExpand> MacroExpands; // 每 TU（同一分片内 TU 串行解析）
    };

    /**
     * @brief 持有"已剥离 PCH 参数"的编译命令集合,替代原 CDB 喂给 ClangTool。
     *
     * @detail 背景:CMake 的 cmake_pch.hxx.gch 是 GCC 生成的,clang libtooling 加载会报
     *         "input is not a PCH file",每个 TU 都解析失败并触发类型错误恢复 —— 必须先把
     *         `-include <pch>` / `-include-pch` / `-Winvalid-pch` 等参数删掉。
     */
    class MPCHStrippedCompilationDatabase : public clang::tooling::CompilationDatabase {
        public:
        explicit MPCHStrippedCompilationDatabase(TVector<clang::tooling::CompileCommand> InCommands) : Commands(std::move(InCommands)) {
        }

        TVector<clang::tooling::CompileCommand> getCompileCommands(llvm::StringRef File) const override {
            TVector<clang::tooling::CompileCommand> Result;
            for (const auto& C : Commands) {
                if (C.Filename == File) {
                    Result.push_back(C);
                }
            }
            return Result;
        }

        TVector<MString> getAllFiles() const override {
            TVector<MString> Result;
            for (const auto& C : Commands) {
                Result.push_back(C.Filename);
            }
            return Result;
        }

        TVector<clang::tooling::CompileCommand> getAllCompileCommands() const override {
            return Commands;
        }

        private:
        TVector<clang::tooling::CompileCommand> Commands;
    };

    /**
     * @brief CDB 装饰器:内层查不到编译命令时,用"参照 TU 的命令行"克隆一条 fallback 命令。
     *
     * @detail 目标是把 *.Async.cpp 这类"codegen 专用源"喂给 ClangTool —— 它们不参与业务编译、
     *         不在 compile_commands.json 里,但 MHeaderTool 必须解析它们才能提取 async 业务体。
     *         参照命令取第一条非 PCH 的业务 TU 命令:include 根、-std、宏、-f 选项都与真实业务
     *         编译一致;手工拼 fallback 极易漏参数(例如缺 -std=c++17 会让 TAwaitable 的
     *         template<auto F> 解析失败),导致函数体遍历不到、AwaitSites 收集为空。
     */
    class MCodegenSourceCompilationDatabase : public clang::tooling::CompilationDatabase {
        public:
        MCodegenSourceCompilationDatabase(TUniquePtr<clang::tooling::CompilationDatabase> InBase, const MString& InSourceRoot) : Base(std::move(InBase)), SourceRoot(InSourceRoot) {
            // 取一条业务 TU 命令作参照——include 根/-std/宏/-f 选项与业务编译
            // 完全一致。手工拼 fallback 极易缺参数(如 -I Build/_deps/... 或
            // TAwaitable 的 template <auto F> 需要 -std=c++17),导致 .Async.cpp
            // 解析失败成 error 节点,函数体遍历不到、AwaitSites 收集不到。
            // 必须避开 PCH 生成命令(cmake_pch.hxx.cxx,带 -x c++-header)。
            const TVector<clang::tooling::CompileCommand> All = Base->getAllCompileCommands();
            for (const auto& C : All) {
                bool bIsPch = false;
                for (const auto& A : C.CommandLine) {
                    if (A.find("c++-header") != MString::npos || A.find("cmake_pch") != MString::npos) {
                        bIsPch = true;
                        break;
                    }
                }
                if (!bIsPch) {
                    ReferenceArgs = C.CommandLine;
                    break;
                }
            }
        }

        TVector<clang::tooling::CompileCommand> getCompileCommands(llvm::StringRef File) const override {
            TVector<clang::tooling::CompileCommand> Cmds = Base->getCompileCommands(File);
            if (!Cmds.empty()) {
                return Cmds;
            }
            if (ReferenceArgs.empty()) {
                return Cmds;
            }
            // 过滤 -o <obj> / -x <lang> / -c,并跳过裸文件参数(参照 TU 源文件
            // 可能不在末尾——MPCHStripped 会在命令末尾追加 -D 宏),再追加新源文件。
            TVector<MString> Args;
            for (size_t I = 0; I < ReferenceArgs.size(); ++I) {
                const MString& A = ReferenceArgs[I];
                if (A == "-o" || A == "-x") {
                    ++I;
                    continue;
                }
                if (A == "-c") {
                    continue;
                }
                if (!A.empty() && A[0] != '-') {
                    continue; // 裸参数 = 源文件,丢弃
                }
                Args.push_back(A);
            }
            Args.push_back(File.str());
            Cmds.push_back(clang::tooling::CompileCommand(SourceRoot, File, std::move(Args), ""));
            return Cmds;
        }

        TVector<MString> getAllFiles() const override {
            return Base->getAllFiles();
        }

        TVector<clang::tooling::CompileCommand> getAllCompileCommands() const override {
            return Base->getAllCompileCommands();
        }

        private:
        TUniquePtr<clang::tooling::CompilationDatabase> Base;
        MString                                         SourceRoot;
        TVector<MString>                                ReferenceArgs;
    };

    /**
     * @brief 给每个 TU 造 MASTReflectionAction 的工厂;把目标 IR 传进去。
     */
    class MFactory : public clang::tooling::FrontendActionFactory {
        public:
        explicit MFactory(SParseIR& IR) : IRRef(IR) {
        }

        TUniquePtr<clang::FrontendAction> create() override {
            return std::make_unique<MASTReflectionAction>(IRRef);
        }

        private:
        SParseIR& IRRef;
    };

    SParseIR MASTPipeline::Run(const SOptions& InOptions) {
        SParseIR IR;

        // ClangTool 的入口:编译数据库。
        // 注意:原始任务书写的是 loadFromJSONFile,而 LLVM 17 的实际 API 叫 loadFromDirectory。
        // loadFromDirectory 只在给定目录下找 compile_commands.json（不会上溯
        // 父目录），而本工程的 json 在 Build/ 下、SourceRoot 是 Source/——
        // 只试 SourceRoot 会加载失败，ClangTool 退到缺系统 include 的 fallback，
        // 导致 cstdint/fmt 找不到、类型被错误恢复（TVector<uint32> → int），
        // 诊断又被 IgnoringDiagConsumer 吞掉，产出静默错误的 .mgenerated。
        // 候选目录：SourceRoot 本身 / 项目根（SourceRoot 父）/ cwd / cwd/Build。
        const fs::path                                  SourceRootAbs = fs::absolute(InOptions.SourceRoot);
        TUniquePtr<clang::tooling::CompilationDatabase> CDB;
        {
            TVector<fs::path> Candidates = {
                SourceRootAbs, SourceRootAbs.parent_path(), fs::current_path(), fs::current_path() / "Build", SourceRootAbs / ".." / "Build",
            };
            for (const auto& Cand : Candidates) {
                MString LoadErr;
                auto    TryCDB = clang::tooling::CompilationDatabase::loadFromDirectory(Cand.generic_string(), LoadErr);
                if (TryCDB) {
                    CDB = std::move(TryCDB);
                    break;
                }
            }
        }

        // CMake 的 PCH（cmake_pch.hxx.gch）由 GCC 生成，clang libtooling 无法
        // 加载（每个 TU 报 "input is not a PCH file"）→ 解析失败、类型错误恢复。
        // 剥离 `-include <pch>` / `-Winvalid-pch` 等参数后重建 CDB。
        if (CDB) {
            TVector<clang::tooling::CompileCommand> Stripped;
            for (const auto& Cmd : CDB->getAllCompileCommands()) {
                clang::tooling::CompileCommand C = Cmd;
                TVector<MString>               NewArgs;
                for (size_t I = 0; I < C.CommandLine.size(); ++I) {
                    const MString& A = C.CommandLine[I];
                    if (A == "-include" || A == "-include-pch") {
                        if (I + 1 < C.CommandLine.size())
                            ++I; // 跳过 PCH 路径
                        continue;
                    }
                    if (A.rfind("-includepch", 0) == 0)
                        continue;
                    if (A == "-Winvalid-pch")
                        continue;
                    NewArgs.push_back(A);
                }
                // await：业务头用 #ifdef MESSION_AWAIT_CODEGEN_SOURCE 保护 TAwaitable 体——
                // 正常编译只见声明，MHeaderTool 解析时开宏才能看到 await 体。
                NewArgs.push_back("-DMESSION_AWAIT_CODEGEN_SOURCE");
                // libtooling 不走 driver，资源头目录必须显式给（见 GetClangResourceDirArg）。
                const MString ResourceArg = GetClangResourceDirArg();
                if (!ResourceArg.empty()) {
                    NewArgs.push_back(ResourceArg);
                }
                C.CommandLine = std::move(NewArgs);
                Stripped.push_back(std::move(C));
            }
            CDB = std::make_unique<MPCHStrippedCompilationDatabase>(std::move(Stripped));
            // *.Async.cpp 等 codegen 专用源不在 compile_commands 里 → 包一层
            // fallback 命令数据库(查不到命令时给 -fsyntax-only + 宏)。
            CDB = std::make_unique<MCodegenSourceCompilationDatabase>(std::move(CDB), SourceRootAbs.generic_string());
        }

        TVector<MString> SourceFiles;
        if (CDB) {
            for (const auto& File : CDB->getAllFiles()) {
                // 性能：compile_commands 含 160 个编译命令，其中大量 TU 不含
                // 业务反射类型（各目标的 PCH 生成 TU、Build/Generated 的
                // .mgenerated.cpp、Tests/、Tools/ 含 MHeaderTool 自身源码），
                // 逐个完整 clang 解析会显著拖慢生成。只保留 Source/ 与
                // Examples/ 下的源文件——反射类型要么在其中定义，要么被它们
                // include 传递带入。
                const MString F = fs::path(File).generic_string();
                if (F.find("/Build/") != MString::npos)
                    continue;
                // Protocol 测试 TU include 业务消息头(MSTRUCT 定义在其中)——需要
                // 解析才能为消息生成反射注册,故豁免 /Protocol/Tests/。
                if (F.find("/Tests/") != MString::npos && F.find("/Protocol/") == MString::npos)
                    continue;
                if (F.find("/Tools/") != MString::npos)
                    continue;
                SourceFiles.push_back(File);
            }
            // 追加 *.Async.cpp(codegen 专用源:async 业务体,不参与业务编译、
            // 不在 compile_commands 里)——递归扫描按后缀识别;编译命令由
            // MCodegenSourceCompilationDatabase 的 fallback 提供。
            {
                TVector<MString> Recursive;
                CollectHeaders(SourceRootAbs, Recursive);
                for (const auto& F : Recursive) {
                    const MString Suffix = ".Async.cpp";
                    if (F.size() <= Suffix.size() || F.compare(F.size() - Suffix.size(), Suffix.size(), Suffix) != 0)
                        continue;
                    const MString P = fs::path(F).generic_string();
                    if (P.find("/Tests/") != MString::npos)
                        continue;
                    if (P.find("/Tools/") != MString::npos)
                        continue;
                    SourceFiles.push_back(P);
                }
            }
        } else {
            // fallback: 构造固定 -fsyntax-only 命令行。
            // 原始任务书只写了 `-fsyntax-only`,既没有 -I 也没有实际源文件列表,
            // 直接把目录当 source 喂给 ClangTool 会报 "expected exactly one
            // compiler job"。补 -I<SourceRoot> 让相对 include 可解析,再递归
            // 收集 .h/.cpp 文件喂给 ClangTool,与 MClangToolRunner 行为对齐。
            const MString IncludeArg = MString("-I") + SourceRootAbs.generic_string();
            // fallback 直接解析 .h 文件：clang 对 .h 默认按 C 语言（struct 成员
            // 初始化在 C 里非法，如 `int X = 0;`）——必须显式 -x c++。
            // 同样开 await codegen 宏（业务头 #ifdef MESSION_AWAIT_CODEGEN_SOURCE 保护 TAwaitable 体）。
            TVector<MString> FallbackArgs = {"-fsyntax-only", "-x", "c++", "-DMESSION_AWAIT_CODEGEN_SOURCE", IncludeArg};
            // 资源头目录同样必须显式给，否则非标准安装路径下 TU 解析全失败。
            const MString FallbackResourceArg = GetClangResourceDirArg();
            if (!FallbackResourceArg.empty()) {
                FallbackArgs.push_back(FallbackResourceArg);
            }
            CDB = std::make_unique<clang::tooling::FixedCompilationDatabase>(SourceRootAbs.generic_string(), FallbackArgs);
            CollectHeaders(SourceRootAbs, SourceFiles);
        }

        if (SourceFiles.empty()) {
            // 一个可解析文件都没有:返回空 IR,由调用方判断是否可接受。
            return IR;
        }

        // 性能:ClangTool::run 本身是单线程的,95 个 TU 串行完整解析约 2 分钟。
        // 这里把 TU 分片,每片一个独立 ClangTool + 独立 SParseIR 并行跑,最后按
        // (QualifiedName, HeaderPath) 等键去重合并 —— 每个分片都会重复产出同一头文件
        // 里的 Record/Enum/FreeFunctions,所以合并不是可选项。
        //
        // NThreads = min(文件数, hardware_concurrency())。标准允许 hardware_concurrency()
        // 返回 0(核数不可推断),此时下面的 `I % NThreads` 会除零,所以兜底至少 1 个分片。
        const size_t              HwThreads = std::max<size_t>(1, static_cast<size_t>(std::thread::hardware_concurrency()));
        const size_t              NThreads  = std::min<size_t>(SourceFiles.size(), HwThreads);
        TVector<TVector<MString>> Shards(NThreads);
        for (size_t I = 0; I < SourceFiles.size(); ++I) {
            Shards[I % NThreads].push_back(SourceFiles[I]);
        }

        TVector<SParseIR>    ShardIRs(NThreads);
        TVector<std::thread> Workers;
        Workers.reserve(NThreads);
        for (size_t T = 0; T < NThreads; ++T) {
            Workers.emplace_back([&, T]() {
                clang::tooling::ClangTool Tool(*CDB, Shards[T]);
                MFactory                  Factory(ShardIRs[T]);
                Tool.run(&Factory);
            });
        }
        for (auto& W : Workers) {
            W.join();
        }

        // 合并各分片 IR（去重）
        for (const auto& Shard : ShardIRs) {
            for (const auto& Record : Shard.Records) {
                bool bMerged = false;
                for (auto& Existing : IR.Records) {
                    if (Existing.QualifiedName == Record.QualifiedName && Existing.HeaderPath == Record.HeaderPath) {
                        // 同一条 Record 出现在多个分片(每个 TU 都 include 头文件):
                        // 合并 Functions——优先保留有体的(定义,如 .Async.cpp 里的),
                        // 无体声明丢弃。否则若声明版分片先合并,定义版被丢,
                        // 类成员 async 实现生成缺失(竞态)。
                        bMerged = true;
                        for (const auto& F : Record.Functions) {
                            bool bFDup = false;
                            for (auto& EF : Existing.Functions) {
                                if (EF.Name != F.Name)
                                    continue;
                                if (EF.AsyncBody.empty() && !F.AsyncBody.empty()) {
                                    EF = F; // 定义替换声明
                                }
                                bFDup = true;
                                break;
                            }
                            if (!bFDup) {
                                Existing.Functions.push_back(F);
                            }
                        }
                        break;
                    }
                }
                if (!bMerged)
                    IR.Records.push_back(Record);
            }
            for (const auto& Enum : Shard.Enums) {
                bool bDup = false;
                for (const auto& Existing : IR.Enums) {
                    if (Existing.Name == Enum.Name && Existing.HeaderPath == Enum.HeaderPath) {
                        bDup = true;
                        break;
                    }
                }
                if (!bDup)
                    IR.Enums.push_back(Enum);
            }
            for (const auto& Fn : Shard.FreeFunctions) {
                bool bDup = false;
                for (const auto& Existing : IR.FreeFunctions) {
                    if (Existing.Name == Fn.Name && Existing.HeaderPath == Fn.HeaderPath) {
                        bDup = true;
                        break;
                    }
                }
                if (!bDup)
                    IR.FreeFunctions.push_back(Fn);
            }
        }

        return IR;
    }

} // namespace mession::headercodegen
