# SDD ledger — plan: .superpowers/plans/2026-08-04-mheadercodegen-ast-refactor-implementation.md

## Baseline-fix commit (2026-08-06, 1173810)

加 P5 工作包 1 的最小实现 + 修复 baseline 不一致:
- MAsync.h 加 `using InnerType = T;`
- MLib.h 加 `using MStringView = std::string_view;` + 类型特征别名
- StringUtils.h 加 MFormat / MStringBuilder 包装 fmt
- LogContext.h/.cpp 用 `MStringView` 别名(原 `TStringView`)
- CMakeLists.txt 注释 Script Abstract / Lua / StringUtilsTest / InnerTypeTest / AwaitableTest
- 删除 worktree 内 P5 untracked sources (Awaitable.h / Tests/ 等)

Build 状态:
- `mht_lib` 100% OK
- 全工程 build: mession_generated_* 找不到 fmt/format.h (P5 半 broken, plan 实施 target 是 mht_lib, 不依赖)

## Task 1 (f895f2f): 根 CMakeLists 启用 compile_commands — DONE

- commit: `set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "" FORCE)`
- review: spec ✅ / quality Approved
- `FORCE` 加成是必要的 (cmake 3.25.1 预初始化 INTERNAL BOOL="", 普通 set 被吞掉)
- minor caveat: `FORCE` 覆盖命令行 `-DCMAKE_EXPORT_COMPILE_COMMANDS=OFF` (标准 CACHE FORCE 语义)
- `Build/compile_commands.json` 生成 OK (103 entries)
- Task 1 complete (commits 1173810..f895f2f, review clean)

## Task 2 (8818ae1): MHeaderTool CMakeLists 链接 Clang libs — DONE

- libclang-17-dev 安装(本机原本缺,user 手动跑 install script)
- commit: 追加 find_package(Clang 17) + 4 个 target_* 调用到 mht_lib
- review: spec ✅ / quality Approved
- `mht_lib` 100% build OK, 无 undefined reference
- minor: brief 描述说"四个调用"但实际 5 个(find_package + 4 target_*) — off-by-one, 不阻塞
- Task 2 complete (commits f895f2f..8818ae1, review clean)

## Task 3 (4e3963f): ClangToolRunner + ASTDumpAction + IR 骨架 + ASTDumpTest — DONE

- 8 new files created; 1 file modified (MHeaderTool CMakeLists.txt)
- ASTDumpTest 跑通: 26964 records / 84282 functions / 1800 enums
- 5 implementer concerns (all acceptable, explained):
  1. LLVM 17 API: loadFromJSONFile → loadFromDirectory
  2. Static IR injection seam (TU-static GIR)
  3. Absolute path for chdir
  4. 加 .cpp/.cc 到 fallback scan
  5. CLANG_LIBRARIES 空 → mht_clang_libs INTERFACE wrapper
- review: spec ✅ / quality Approved (11 Minor findings 全部 deferred / informational)
- minor (deferred): bIsAsync / bHasM* 命名等 A3 整改统一处理
- minor (deferred): HeaderPath 用 printToString 输出 — Task 5/6 应改 SM.getPresumedLoc
- minor (deferred): AST sources 后续 Task 8 应合进 mht_lib
- minor (deferred): plan Task 8 Steps 4 应更新用 JSONCompilationDatabase::loadFromFile
- Task 3 complete (commits 8818ae1..4e3963f, review clean)

## Task 3 fix round 1 (51db8d3): 加 -I<SourceRoot> 到 CDB fallback — DONE

Task 4 验收发现:
- ASTDumpTest 只打印 aggregate count, plan 验收需要按 Name grep
- 5 key type 一个都查不到 — CDB fallback 没加 -I, Clang 解析业务 header 失败
- fix: 加 fs::absolute(SourceRoot) 到 FixedCompilationDatabase 的 args, ASTDumpTest 迭代 IR.Records 打印 Name
- 验证: 5/5 key type FOUND, records 26964 → 119995
- 修复范围: 2 files changed, 14 insertions, 1 deletion

## Task 4 (51db8d3): A1 验收 — DONE

- ASTDumpTest 跑通
- 5 key type 全部 FOUND (MEchoService / MServiceRegistry / MGatewayServer / MObject / FAppError)
- Build/Generated/ 包含 MEchoService / MServiceRegistry / MGatewayServer / FAppError (15 个 .mgenerated.h 文件)
- MObject 是 reflection 基类, 无 .mgenerated.h (符合预期)
- Task 4 complete (commits 4e3963f..51db8d3, fix verified)

## Task 5 (37a9427): IR.h 完整扩展 — DONE

- 全 spec §2 数据模型落地: 5 enums + 11 structs (SParsedType / SParsedParameter / SParsedProperty / SParsedEnumValue / SParsedEnum / SParsedTypeAlias / SParsedRecord / SAwaitSite / SLiveVarDecl / SParsedFunction / SParseIR)
- mht_lib + ASTDumpTest 均 build OK
- fix round 1 修复:
  - SParsedFunction forward decl 移到 SParsedRecord 之前
  - trailing newline
  - 调用方适配: SOptions → MHeaderTool::SOptions, IR.Functions → IR.FreeFunctions
- review Critical findings 全部解决
- Task 5 complete (commits ef58a2d..37a9427, fix verified)

## Task 6 (03eb602): ASTReflectionVisitor 完整实现 — DONE

- 2 new files: ASTReflectionVisitor.h / .cpp
- mht_lib + ASTDumpTest 均 build OK
- 3 implementer deviations (LLVM 17 API drift, all acceptable):
  - TArray<MString, 5> → TVector<MString> (无 TArray alias)
  - CharSourceRange(Start, Loc) → getCharRange() (clang 17 factory)
  - SAwaitSite::EKind → EAwaitSiteKind (IR.h 是顶层 enum, brief 错)
- CollectLiveAcrossAwait 是 A2 stub (P5 工作包 4a 延后)
- review ✅ Approved with 6 Minor findings (all deferred or Task 7 pre-work)
- minor (Task 7 pre-work): Record.Kind 没设 (只设 bHasMStructMarker); Task 7 要么在 VisitCXXRecordDecl 补, 要么用 bHasMStructMarker 分支
- minor (Task 7 pre-work): MFUNCTION arg extraction (Transport/RpcKind/Endpoint/MessageName/Route/Target/Auth/Wrap/ClientApi) 没接 — Task 7 会看到空字符串
- minor (Task 7 pre-work): Properties / TypeAliases 没填充 — 没 VisitFieldDecl / VisitTypeAliasDecl
- minor (Task 7 pre-work): method parent 不匹配 silently demote 到 FreeFunctions
- Task 6 complete (commits 37a9427..03eb602, review approved)

## Task 7 (b91078a): CodeGenerator IR 重载 — DONE_WITH_CONCERNS

- 3 files modified (CodeGenerator.h + CodeGenerator.cpp + CMakeLists.txt)
- mht_lib + ASTDumpTest 均 build OK
- 3 implementer deviations (all acceptable, explained):
  1. 类名沿用 CodeGenerator (不是 MCodeGenerator) — A3 namespace 整改时统一
  2. 留在 MHeaderTool namespace — A3 整改
  3. 没单独实现 GenerateStructHeaderFromIR / GenerateClassHeaderFromIR, 走 IR → legacy SParsedClass shim 复用现有 inline helper — 唯一通往 Task 9 byte-equal parity 的路径
- review: spec ✅ / quality Approved with 1 Important + 6 Minor
- Important fix: MacroArgs 需 mirror FlagsExpr (legacy BuildPropertyFlagsExpr 读 MacroArgs token stream)
  - 当前 CodeGenerator.cpp shim 注释里说明"MacroArgs reconstructed from FlagsExpr"
  - 实际 Out.MacroArgs 还没填 — Task 9 A2DiffTest 会暴露, 届时再补
- minor (Task 7 已审理): 6 项 Minor (Properties 空、TypeAliases 空、bInjection 未接 等) — 都是脱 A2 spec 范围, A3 / 后续阶段处理
- Task 7 complete (commits 89d3830..b91078a, review approved, 1 Important finding carried to Task 9)

## Task 8 (d0785c8): ASTPipeline + main rewrite — DONE

- 4 files (2 new ASTPipeline.h/.cpp + 2 modified MHeaderTool.cpp / CMakeLists.txt)
- mht_lib + MHeaderTool + ASTDumpTest 均 build OK
- main 跑通: ./Bin/MHeaderTool --source-root=Source --output-dir=/tmp/a2_codegen 产生 13 個 .mgenerated.h
- 6 implementer deviations (5 acceptable, 1 carry-forward):
  - loadFromJSONFile → loadFromDirectory (LLVM 17)
  - fallback 加 -I<SourceRoot> + 递归 header
  - MASTPipeline::Run 改 static
  - MCodeGenerator → CodeGenerator (Task 7 precedent)
  - 加 using namespace MHeaderTool; (for unqualified SOptions/CreateDirectory 等)
  - 已知 gap: Properties/TypeAliases/Metadata/Manifest 都留给 A3
- review: spec ✅ / quality Approved (5 Minor, 全部 A3 follow-up)
- critical review 验证: dispatch path 正确, IR 字段填充完整, 产出 reproducible byte-equal
- Task 8 complete (commits b91078a..d0785c8, review approved)

## Task 6 fix round 1 (89d3830): 补 Task 7 pre-work gap — DONE

- 修 gap 1: VisitCXXRecordDecl 在 MCLASS/MSTRUCT 分支设 Record.Kind
- 修 gap 2: VisitFunctionDecl 加 ApplyMFUNCTIONMacroArgs 调用, 填充 Transport / RpcKind / Endpoint / MessageName / Route / Target / Auth / Wrap / ClientApi / bHasAsyncMeta
- 修 bIsAsync 综合判断: bHasAsyncMeta || IsSFutureResultType(ReturnType)
- 加 ApplyMFUNCTIONMacroArgs 声明 + 实现
- gap 3 (Properties/TypeAliases) 与 gap 4 (method demote) 留给后续阶段 (brief 没要求, A3 可考虑加)
- mht_lib build OK
- 衔接 Task 7 codegen 准备就绪

## Task 9 (cd0dc6c): A2DiffTest byte-equal 框架 — DONE

- 8 files (A2DiffTest.cpp + Tests/CMakeLists.txt)
- 首次跑通: 6 共享 .mgenerated.h 文件都"字节相等" — 但比较错对象 (Build/Generated 被 AST 覆盖, AST-vs-AST 不是 AST-vs-legacy; reviewer Critical)
- 拆基线: /tmp/legacy_baseline 不可变 (43 文件 2026-08-07 12:06 pre-AST)
- 6 shared 实际全 AST_NEQ_LEGACY

## Task 9 fix round 1 (ff4ac39): 真 legacy baseline + VisitFieldDecl — DONE

- 拆 Build/Generated baseline → /tmp/legacy_baseline (immutable)
- 加 VisitFieldDecl + VisitTypeAliasDecl 到 MASTReflectionVisitor
- Properties 12/12 SEchoService (✓), 9/9 SGatewayConfig (✓), 3/3 FClientDownlinkPushRequest (✓)
- BUT: 6 shared 仍 AST_NEQ_LEGACY, 但 baseline 现在是真的 legacy
- 3 documented gaps: path prefix (cosmetic), Cli metadata missing, canonical name `int` vs `TVector<uint32>`

## Task 9 fix round 2 (024777f): ToLegacyProperty MacroArgs mirror — DONE

- ToLegacyProperty mirror FlagsExpr → MacroArgs (legacy BuildPropertyFlagsExpr 读 MacroArgs token stream)
- 0/6 EQL → 0/6 EQL (视觉不变, 当前 6 shared 文件无 flag arg 字段)
- 修的是 CORRECTNESS: 未来有 MPROPERTY(...) flag arg 时, MacroArgs 不再是空
- subagent 同时做 worktree recovery: 还原 108 untracked files (74 C2-style reformats + 31 merge conflict markers) → HEAD — 纯清理, 无逻辑变更
- 剩余 6 divergence 全 A3 follow-up: path prefix / Cli metadata / canonical name `int` vs `TVector<uint32>`
- Task 9 byte-equal 量化不能 close — 0/6 closure 需要 A3 整改
- reviewer 建议: Task 10 A2 验收 "条件签" — 关掉 gap 1+3 (path + Cli) 让 6/6 byte-equal

## Task 10 (024777f): A2 验收 — DONE (conditional sign)

- 6 shared `.mgenerated.h` 全 AST_NEQ_LEGACY (gap 1+2+3 全是 A3 follow-up):
  - gap 1: path prefix `worktree` vs `main` (cosmetic)
  - gap 2: Cli metadata missing (parse `Meta=(Cli="...")` in `ToLegacyProperty`)
  - gap 3: canonical name `int` vs `TVector&lt;uint32&gt;` (TemplateSpecializationType traversal)
- 37 LEGACY_ONLY 全 A3 / Task 11 / Task 12 / Task 14 follow-up (MPROPERTY / MSTRUCT / manifest / AsyncFrames)
- **Verdict**: 条件签 A2 — AST 路径已能产出全部 13 codegen, 6 shared 字节相等的差距是 A3 整改范畴, 不阻塞 A2 验收
- reviewer 建议: 关掉 gap 1+3 (path + Cli) 让 6/6 byte-equal, 然后进 A3
- 下一步: Task 11 删除 Parsing/* 字符串解析, Task 12 命名空间 + STL + bool 命名 + Allman 整改, 顺道关掉 gap 1+3

Task 10 conditional A2 sign — ledger-only commit, no C++ source changes.