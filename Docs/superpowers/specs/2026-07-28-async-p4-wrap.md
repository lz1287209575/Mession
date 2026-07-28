---
description: P4 wrap-up for the cpp17 async/await refactor — extend
  MFUNCTION(Async) to namespace-scope free functions (replace MASYNC),
  delete the legacy Fiber MAwait/MAwaitOk API surface, sync the parent
  spec from "deprecated" to "removed", and add a CLAUDE.md quick reference.
  Implementation status: planned (2026-07-28); dependent on P0/P1/P2/P3
  already landed on main (commits 13d3587, e7da4bc, 558f7a9, 1b69123).
---

# C++17 Async / Await — P4 收口(实施 spec)

> 起草:2026-07-28
> 状态:实施 spec(经 brainstorming 确认)
> 范围:父 spec `Docs/superpowers/specs/2026-07-24-cpp17-async-await.md` 的 **P4** 阶段
> 关联:
> - 父 spec(异步模型全图;P4 收口后 §2 / §6.2 / §7.4 / §14 / §16.8 / §17 需同步升级)
> - `Docs/superpowers/specs/2026-07-25-cpp17-p0-cleanup.md`(P0 实施 spec)
> - `CLAUDE.md` / `TODO.md`(PoC 拓扑 + backlog)
> - `Source/Common/Runtime/Async/{MAsync.h,AsyncContext.h,AwaitMacros.h}`
> - `Source/Common/Runtime/Concurrency/FiberAwait.h`(本次主删除面)
> - `Source/Tools/MHeaderTool/{MHeaderTool.cpp,Parsing/FunctionParser.h,Generation/CodeGenerator.h}`
> - `Source/Servers/EchoService/EchoService.{h,cpp}` / `Source/Common/Net/Rpc/MRpcChannel.h`(调用面)

---

## 0. 一句话

把 `MFUNCTION(Async)` 扩展到自由函数(替代 spec §6.2 中提到的 `MASYNC`),删除 `FiberAwait.h` 中 `MAwait` / `MAwaitOk` / `TPlayerCommandFuture` 整套遗留 API,同步升级父 spec 从 `deprecated` 到 `removed`,并在 `CLAUDE.md` 加精简 async 快查表。**单 PR**,不引入运行时行为改动、不动 MFiberScheduler 基础设施。

---

## 1. 范围 / 非范围

### 1.1 在本 PR

1. `MHeaderTool`:新增「namespace-scope / 自由函数 + `MFUNCTION(Async)`」扫描与 Frame 代码生成路径;拒绝自由函数上的 `ServerCall` / transport tag。
2. `MHeaderTool` 已有 class-method `MFUNCTION(Async)` codegen 路径**不动**(P3 已落地)。
3. `FiberAwait.h`:删除 `MAwait` / `MAwaitOk` 全部重载(含已被 `[[deprecated]]` 包裹的 `TPlayerCommandFuture` 重载),删除 `TPlayerCommandFuture` alias(删除后无 consumer)。
4. `MRpcChannel.h`:重写 doxygen 中 `FResponse result = MAwaitOk(response);` 引用(对齐新约定)。
5. `MAsync.h:237` 重写「与现有 `TPlayerCommandFuture` 兼容」注释。
6. 父 spec `2026-07-24-cpp17-async-await.md`:同步 §2 / §6.2 / §7.4 / §14 / §16.8 / §17 Q4 从「deprecated / MASYNC」改为「removed / 统一用 `MFUNCTION(Async)`」。
7. `CLAUDE.md`:`Recommended reading` 段附近加 5-10 行 async 快查表,指向父 spec + 本 spec。
8. 新增单测:`MHeaderTool` 自由函数 `MFUNCTION(Async)` 输入样例 → 验证 Frame header 生成 + `ServerCall` 误用报错。

### 1.2 不在本 PR(明确推迟)

- `MFiberScheduler.h` / `CommandExecutionContext.h` / `MPlayerCommandContext` / `FPlayerCommandAbort` / `FPlayerCommandError` —— 底层 player-command 基础设施,**保留**;独立后续 PR 退役。
- `MEventAwait.h` 等业务层现有 `SFutureResult<T>` 返回值的类成员方法 —— 不动,继续使用 `MFUNCTION(Async)` 或同步 `SFutureResult` 已就绪路径。
- `MAwait` 删除后的「断点恢复/测试 stub」兜底 —— 不引入;若业务层调用方有需要,在后续 PR 单独提供。
- CodingStyle C2–C5 全量 format —— 已知 backlog,不动。
- `MAsync.h:237` 之外任何 doxygen 重写(范围爆炸)。

非范围写在这里是为了**保护本 PR 不被继续扩张**。

---

## 2. 决策记录(已确认)

| ID | 决策 | 备选 | 选此的理由 |
|----|------|------|-----------|
| D1 | **`MFUNCTION(Async)` 同时支持类成员 + 自由函数**;不引入 `MASYNC` | A. 引入 `MASYNC` 作为新宏 / B. 保留 `MASYNC` 作为 `MFUNCTION(Async)` 的语义糖 / C. 仅 class-method | 统一入口;spec §6.2 提到 `MASYNC` 但 P0–P3 全部用例都是类成员,自由函数无 consumer;若真出现自由函数 `AWAIT_OK` 需求,`MFUNCTION(Async)` 一并覆盖 |
| D2 | **删除 `MAwait` / `MAwaitOk` 全部重载 + `TPlayerCommandFuture` alias** | A. 仅加 `[[deprecated]]` / B. 仅删 `MAwaitOk(SFutureResult)` 保留 `MFuture` 重载 | 父 spec §16.8 写"legacy 非主模型",P0 已删 `MFUTURE` 包装,语义已对齐;C# 模型根本不需要 fiber 挂起;保留只会留双模型混用风险 |
| D3 | **父 spec §2 / §6.2 / §7.4 / §14 / §16.8 / §17 Q4 同步升级** | A. 仅在本 PR spec 中描述、不改父 spec / B. 在 P5 再合 | P4 的语义改动(从 deprecated 到 removed)是父 spec 决策的延伸;必须同 PR 升级,否则读者会看到矛盾 |
| D4 | **新 P4 spec 文件 `2026-07-28-async-p4-wrap.md`** | A. 不写 / B. 更新父 spec | 与 P0 cleanup spec 同样的存档传统;保留父 spec 修订历史清晰 |
| D5 | **`MFiberScheduler.h` 等基础设施保留** | A. 同步删 / B. 仅删 MAwait 调用 | 删 `MAwait` 后 `MFiberScheduler` / `MPlayerCommandContext` 等仍有真实消费者(`SuspendCurrentCommandUntil` 等基础设施);P4 不动基础设施,留给独立 PR |
| D6 | **CLAUDE.md 仅加精简快查表** | A. 全量 async 指南 / B. CLAUDE.md 不动只改 spec | 推荐项;全量指南会与父 spec 重复;CLAUDE.md 完全不提会让新人不走偏 |
| D7 | **单 PR 推进** | A. 拆两 PR(MHeaderTool + Fiber 删除) / B. 拆三 PR | 三件事虽松耦合,但都是「P4 收口」主题;CLAUDE.md 快查表在另两 PR 后再加会让人误以为「还没收口」 |

---

## 3. 待修改文件清单

### 3.1 MHeaderTool 自由函数 codegen(§B)

| 文件 | 改动 |
|------|------|
| `Source/Tools/MHeaderTool/MHeaderTool.cpp` | 新增 `ProcessFreeFunctions(headerText)` 路径;在 `allClasses` 收集阶段同步收集 `SFreeAsyncFunc` 列表(只识别 `MFUNCTION(Async)`,拒绝 `MFUNCTION(ServerCall, Async)` 等带 transport 的形式) |
| `Source/Tools/MHeaderTool/Generation/CodeGenerator.{h,cpp}` | 新增 `EmitFreeAsyncFramesHeader(collectedFuncs)` —— 输出 `<Header>_FreeAsyncFrames.mgenerated.h`,含每个自由函数一个 `MHeaderTool_AsyncFrame_Free_<FuncName>` struct(`AwaitOk` pass-through + State 字段 + 外层 `MPromise`) |
| `Source/Tools/MHeaderTool/Parsing/FunctionParser.h` | **可选重构**:若发现 class-method 路径与自由函数路径共享 ≥3 行 token 解析逻辑,抽出 `ParseAsyncFunctionDeclaration(macroArgs, decl)` 公共函数;否则保持两份独立(避免无意义抽象) |
| `Source/Tools/MHeaderTool/Core/Types.h` | 新增 `SFreeAsyncFunc { string HeaderPath; string Name; string ResponseType; string AsyncBody; }` |

### 3.2 FiberAwait.h 删除(§C)

| 文件 | 改动 |
|------|------|
| `Source/Common/Runtime/Concurrency/FiberAwait.h` | 删除 `MAwait(MFuture<T>)` template + impl;删除 `MAwait(MFuture<void>)`;删除 `MAwaitOk(SFutureResult<T>)`;删除 `MAwaitOk(TPlayerCommandFuture<T>)` (已被 `[[deprecated]]` 包裹);删除 `MAwaitOk(SFutureResult<void>)`;删除 `MAwaitOk(TPlayerCommandFuture<void>)`;删除 `TPlayerCommandFuture` alias |
| `Source/Common/Runtime/Concurrency/FiberAwait.cpp` | **不动**(该文件不含 MAwait/MAwaitOk 实现,只放 `MPlayerCommandDetail::*` 与 `MHasCurrentPlayerCommand` 等保留函数) |
| `Source/Common/Net/Rpc/MRpcChannel.h:26` | doxygen 块重写,删除 `FResponse result = MAwaitOk(response);` 引用,改为 `auto fut = ...; fut.Then(...)` 形态或纯指向父 spec §18 附录 A |
| `Source/Common/Runtime/Async/MAsync.h:237` | 注释行「便捷别名(与现有 `TPlayerCommandFuture` 兼容)」重写为「兼容 `MFuture<TResult<T, FAppError>>` 形态;业务统一走 `SFutureResult<T>`(spec §5.1)」 |

### 3.3 父 spec 同步(§D)

| 文件 | 改动 |
|------|------|
| `Docs/superpowers/specs/2026-07-24-cpp17-async-await.md` | §2 第 2 条 `MAwait` 改为「`MAwait` 自 P4 起删除;Fiber 基础设施保留到独立 PR」;§6.2 重写为「自由函数用 `MFUNCTION(Async)`;不引入 `MASYNC`」并删除 `MASYNC` 宏示例;§7.4 表格删除 `MAwait` 列;§14 P4 行内容更新为本次实际收口;§16.8 改为「Fiber = legacy,非 C# 向主模型(`MAwait` 自 P4 起删除)」;§17 Q4 删除 |
| `CLAUDE.md` | `Recommended reading` 段**之前**新增一节「C++17 异步模型快查」(5-10 行,贴 spec §18 附录 A 核心示例 + 指向父 spec 与本 spec) |

### 3.4 单测

| 文件 | 改动 |
|------|------|
| `Source/Tools/MHeaderTool/Tests/` | 新增 `FreeAsyncFuncGenTest.cpp`:输入样例含 `MFUNCTION(Async)` 自由函数 → assert `EmitFreeAsyncFramesHeader` 输出包含期望 Frame struct;另一 case 含 `MFUNCTION(ServerCall, Async)` 自由函数 → assert 报错信息(spec §6.2) |
| `Source/Servers/EchoService/Tests/main.cpp`(P3 已有) | **不动**;本次不新增 case,验证 P3 已有 3 case 仍绿 |

---

## 4. 顺序与依赖

```text
Step 1(独立)              Step 2(独立)              Step 3(必须在 1,2 之后)         Step 4(独立)
FiberAwait.h 删除        MHeaderTool 自由函数     父 spec 同步升级                CLAUDE.md 快查表
       │                        │                            │                              │
       ▼                        ▼                            ▼                              ▼
  Source/Common/...         Source/Tools/...           Docs/superpowers/...            CLAUDE.md
       │                        │                            │                              │
       └────────────── 全量 build 验证 ──────────────────────┴────────────── verify_protocol ─────┘
```

依赖说明:
- Step 1 与 Step 2 无编译期依赖,可任意顺序或并 PR(本 PR 合并为一)
- Step 3 必须在 Step 1 后:`MAwait` 从 spec 删除后才能在文中以「removed」形式描述;否则会有遗留接口 vs 描述矛盾
- Step 4 与前三者完全独立;但合并到同 PR 是因为同主题

---

## 5. 验收

| 项 | 验证手段 |
|----|----------|
| 编译 | `cmake --build Build -j4` 0 报错 / 0 警告(除已存在的 `MFiberScheduler.h` 自身 deprecated) |
| MHeaderTool 单测 | 新增 `FreeAsyncFuncGenTest` 全部 case 通过;P3 已有 `AsyncFrameTest` 3 case 仍绿 |
| `MAwait` 残留 | `grep -rn 'MAwait\|MAwaitOk\|TPlayerCommandFuture' Source/ Build/Generated 2>/dev/null` 仅剩 `MFiberScheduler.h` 等保留基础设施文件中对 `MPlayerCommandContext` 等**其他**概念的引用 |
| `MASYNC` 残留 | `grep -rn 'MASYNC' Source/ Build/Generated Docs/ 2>/dev/null` 仅剩父 spec 修订历史与本 spec 中的解释性文字 |
| 协议反射一致 | `Scripts/verify_protocol.py` 不变(本次不动 MHeaderTool 反射路径) |
| `MRpcChannel.h` doxygen | 第 26 行无 `MAwaitOk` 引用;与父 spec §18 附录 A 风格一致 |
| `MAsync.h` 注释 | line 237 无 `TPlayerCommandFuture` 提及 |
| 父 spec 章节 | §2 / §6.2 / §7.4 / §14 / §16.8 / §17 Q4 升级到「removed / 统一 MFUNCTION(Async)」;前后无矛盾 |
| CLAUDE.md | 「C++17 异步模型快查」段存在,5-10 行,指向父 spec 与本 spec |
| 单 PR 文件数 | 预计 ≤ 12(2 个 MHeaderTool 头 + 1 个 cpp + 1 个 FiberAwait.h + 1 个 MRpcChannel.h + 1 个 MAsync.h + 1 个父 spec + 1 个 CLAUDE.md + 1 个本 spec + 1 个测试 cpp + 1 个测试 fixture 文件),可接受 |

---

## 6. 风险与缓解

| 风险 | 缓解 |
|------|------|
| MHeaderTool 自由函数扫描跨 include 边界、解析失败 | 第一版仅支持**单文件直接定义**的自由函数;跨文件自由函数在 follow-up 支持;扫描阶段失败 case 在测试样例里以 reject 形式断言 |
| `MAwait` 删除后现存代码意外编译失败 | 全量 `cmake --build` 在 Step 1 后立即跑;若发现调用点,本 PR 同步改写(已知 0 调用点,见 §C 风险评估) |
| `TPlayerCommandFuture` 删除后 `MFiberScheduler.h` 中有隐藏使用 | 实施 Step 1 前 `grep -rn 'TPlayerCommandFuture' Source/` 二次确认(已知仅 `FiberAwait.h` 内部使用,见 §C) |
| 父 spec 升级与父 spec 既有引用(其他文档)产生矛盾 | 同步扫 `Docs/` 与 `CLAUDE.md` 对 `MASYNC` / `MAwait` 的引用;PR 同步更新或加 TODO 注释 |
| `MFUNCTION(ServerCall, Async)` 自由函数误用报错不友好 | 错误信息明确包含「spec 2026-07-24 §6.2 + spec 2026-07-28 §B」路径,便于用户跳转 |
| MHeaderTool 新增 `SFreeAsyncFunc` 类型破坏现有生成路径 | 复盘 P3 提交 1b69123 的设计;`SParsedClass` 不动;新类型是平行的 sibling,无 join |
| 单测 fixture 与已有 `EchoService` 冲突 | 新测试用独立 fixture 文件 + 独立输出目录;不复用 `MEchoService_AsyncFrames.h` 命名 |

---

## 7. 开放问题(实施前可再拍板)

| ID | 问题 | 默认倾向(本 PR 沿用) |
|----|------|-------------------------|
| Q1 | 自由函数 `MFUNCTION(Async)` Frame header 命名:`<Header>_FreeAsyncFrames.mgenerated.h` 还是 `<Header>_AsyncFree.h` / 其他? | **`<Header>_FreeAsyncFrames.mgenerated.h`** —— 与 P3 既有 `<ClassName>_AsyncFrames.h` 命名对齐 |
| Q2 | 自由函数上 `MFUNCTION(Async)` 是否允许带其他非 transport 修饰(如 `Reliable=`)? | **本期不允许** —— 仅识别纯 `Async` tag;后续按需放宽 |
| Q3 | `CLAUDE.md` 快查表具体放哪? | **`Recommended reading` 段之前**,作为新加一节「C++17 异步模型快查」;不动既有目录结构 |
| Q4 | 删除 `TPlayerCommandFuture` 时,是否同步清理 `MAsync.h:237` 的"兼容"注释? | **是**(已在 §3.2 列出) |

---

## 8. 修订历史

| 日期 | 说明 |
|------|------|
| 2026-07-28 | v0:P4 收口实施 spec 起草;记录 D1-D7 决策;brainstorming 与用户对齐后落稿 |