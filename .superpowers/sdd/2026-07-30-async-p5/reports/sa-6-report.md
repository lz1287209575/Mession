---
description: SA-6 子代理报告 — 把 SA-1 ~ SA-5 输出合成完整 P5 spec 主文档,完成 14 节结构 + 19 KD 引用 + 7 Open Q 状态锁定 + 跨 spec 引用一致性检查。
---

# SA-6 Report — 整合最终 P5 spec

## Status

**DONE** — 主文档合成完成,KD 一致性检查通过,Open Q 状态锁定。

---

## 输出

- **主文档**:`/root/Mession/Docs/superpowers/specs/2026-07-30-async-p5.md`(**3193 行**,14 节结构齐全)
- **子 spec 保留**:5 个独立 spec 不动(`Docs/superpowers/specs/2026-07-30-async-p5-{1,2,3,4,5}-*.md`)
  - SA-1:~500 行(§1 目标 + §2 非目标 + §3 Open Q 原始)
  - SA-2:697 行(§3 + §4 + §9 底层契约)
  - SA-3:487 行(§2 awaitable 硬标准)
  - SA-4:882 行(§5-§8 + §9-§12 codegen 主体)
  - SA-5:1095 行(§11 + §12 + §13 迁移 + AsyncDemo2)

### 主文档章节结构(14 节)

| 章节 | 标题 | 来源 |
|---|---|---|
| §1 | 目标与非目标 | SA-1 §1 + §2 |
| §2 | awaitable 硬标准 — 用法层面 | SA-3 §2(整合时把 §2.4.3 循环 await 改成"P5 解禁") |
| §3 | SFutureResult awaiter 协议扩展 | SA-2 §3 |
| §4 | `TAwaitable<F, R, Args...>` 模板 | SA-2 §4 |
| §5 | codegen + clang libtooling 集成 | SA-4 §5 + §10.1(合并 KD-11) |
| §6 | AST → 状态机 IR 转换 | SA-4 §6 |
| §7 | 多 slot 状态机代码生成 | SA-4 §7 |
| §8 | 多 await 串行执行语义 | SA-4 §8 |
| §9 | codiagnostics | SA-2 §9 + SA-4 §9 |
| §10 | Open Questions | SA-1 §3 + SA-4 §10.1 + SA-5 §11.3/§11.5/§11.6 |
| §11 | P4 v1 迁移路径 | SA-5 §11 |
| §12 | AsyncDemo2 示例 | SA-5 §12 |
| §13 | 跨 spec 引用与依赖汇总 | SA-6 新写 |
| §14 | 修订历史 | SA-6 新写 |

---

## KD 汇总(19 条 — 全部 verbatim 引用)

| KD | 锁定内容 | 来源章节 | 主文档位置 |
|---|---|---|---|
| **KD-1** | `SAwaiter` 三方法签名(`AwaitReady` / `AwaitSuspend<FrameT*>` / `AwaitResume`) | SA-2 §3.2 | §3.2 |
| **KD-2** | `TAwaitable<F, R, Args...>` 模板签名(`F` + `R` + `Args...` + `StoredArgs` + `AsAwaiter()`) | SA-2 §4.1 | §4.1 |
| **KD-3** | `R` 推导规则(形式 A 从 `MFUNCTION(Async)` 签名推;形式 B 从 `F = SFutureResult<R>` 推;业务代码侧必须写完整三参 / 两参) | SA-2 §4.3 | §4.3 |
| **KD-4** | codiagnostics 6 类错误(A 同步函数 / B 不存在 / C 形式 B 错 / D 参数不匹配 / E 非 async 函数 / F 嵌套) | SA-2 §9.1 | §9.1 |
| **KD-5** | codiagnostics 实现(用 `clang::DiagnosticsEngine` + `.td` TableGen + 默认英文) | SA-2 §9.3 | §9.3 |
| **KD-6** | 业务侧不可见元素(`StoredArgs` / `AsAwaiter()` 返回类型 / 续体 lambda / Frame 类型名 / `Frame->Slots[] Post() Resume()`) | SA-2 §4.5 | §4.5 |
| **KD-7** | 业务侧两种 await 表达式唯一允许的形态(`TAwaitable<F, R>(args...)` / `TAwaitable<F, R>()`) | SA-3 §2.1 | §2.1 |
| **KD-8** | 业务侧 12 项禁止形态(映射到 KD-4 6 类错误) | SA-3 §2.1.3 | §2.1.3 |
| **KD-9** | 多 await 状态机外部契约 5 条(顺序 / 副作用 / 错误传播 / 无 cancellation / 无 timeout) | SA-3 §2.5 | §2.5 + §8.1-§8.4 |
| **KD-10** | 退化场景 3 项支持策略(同步 Async / if-else / for-while) | SA-3 §2.4 | §2.4 |
| **KD-11** | codegen 触发机制(mtime-based AST 缓存 + 增量重解析) | SA-4 §10.1 | §5.5 |
| **KD-12** | 状态机 IR 形态(`SStateMachineIR { FuncName, ReturnType, LiveAcrossAwait, States, InitialState }`) | SA-4 §6.1 | §6.1 |
| **KD-13** | 循环 await 状态机形态(LoopEntry + LoopBody + LoopExit 复合 + Frame.LoopIndex) | SA-4 §7.3 | §7.3 |
| **KD-14** | codegen 输出可读性 4 条(state 连续编号 + source line 注释 + 朴素 lambda + 无宏) | SA-4 §7.6 | §7.6 |
| **KD-15** | P4 v1 替换策略(全替换 + 业务代码迁移 + build system 行为) | SA-4 §11.3 / §7.5 | §11.5 |
| **KD-16** | P4 v1 共存期 = 0(无 P4 + P5 并行 codegen 路径) | SA-5 §11.0 + §11.5 | §11.5 |
| **KD-17** | AsyncDemo 共存期(**1-2 release + 路径 B(注释化)**) | SA-5 §11.6 | §11.6 |
| **KD-18** | 迁移工具脚本 `MHeaderTool --migrate` 子命令 | SA-5 §11.4 | §11.4 |
| **KD-19** | codegen Fix-it hint 提供策略(行替换 ✅ / 类型名删除 ❌ / 多行重构 ✅ / `.Get()` 误用 ⚠️ 警告) | SA-5 §11.3 | §11.3 |

---

## Open Q 最终状态(7 条 — 主会话 2026-07-30 拍板)

| Open Q | 主题 | 最终状态 | 决定来源 | 备注 |
|---|---|---|---|---|
| **Q1** | libtooling AST 缓存策略 | **已拍(KD-11)** | SA-4 §10.1 / §5.5 | mtime-based AST 缓存 + 增量重解析 |
| **Q2** | clang minor 版本矩阵 | **待主会话协调** | SA-1 §3 Open Q 2 | clang ≥ 15;具体 minor 版本组合 CI matrix 落地时定 |
| **Q3** | Windows libtooling MT vs MD | **待 Windows owner 协调** | SA-1 §3 Open Q 3 | LLVM default MD vs 项目 MT 选型;SA-4 实施 PR 期间拍 |
| **Q4** | P4 v1 共存期(强制截止日期) | **已拍(KD-16)** | SA-5 §11.0/§11.5 | **P4 v1 共存期 = 0**(无 P4 + P5 并行 codegen 路径) |
| **Q5** | AsyncDemo vs AsyncDemo2 并存期 | **已拍(KD-17)** | SA-5 §11.6 | **路径 B(注释化) + 1-2 release** |
| **Q6** | codegen IDE 集成 / Fix-it hint | **已拍(KD-19)** | SA-5 §11.3 | Fix-it hint 提供策略见 KD-19 |
| **Q7** | 状态机调试体验 / sourcemap | **P6+** | SA-1 §3 Open Q 7 | P5 v1 不生成 sourcemap;debugger 看 Frame 字段 + Resume switch 表 + `// source line N` 注释(§8.5) |

### 各 Open Q 的负责方与拍板时机

- **Q1(Q4/Q5/Q6)**:已拍板,SA-4 / SA-5 在 spec 起草期间锁定。
- **Q2**:主会话协调,CI matrix 落地时定。具体 minor 版本组合(15 / 16 / 17 / 18)的兼容性矩阵由 CI 矩阵负责人决定。
- **Q3**:Windows 构建 owner 协调,SA-4 codegen 实施 PR 期间(预计 2026 Q4)拍。
- **Q7**:P6+,与状态机 sourcemap / 调试体验相关独立 PR;P5 v1 不解决。

---

## 跨 spec 一致性检查

### SA-1 ~ SA-5 互相引用的章节号一致性

| 引用关系 | 主文档位置 | 子 spec 引用位置 | 一致性 |
|---|---|---|---|
| §1 目标 → §2 用法 | §1 目标 6 → §2.1.2 | SA-1 §1 → SA-3 §2.1.2 | ✅ |
| §2 用法 → §3 协议 | §2.1.3 引用 §3 | SA-3 §2.1.3 → SA-2 §3 | ✅ |
| §2 用法 → §4 模板 | §2.1.1 引用 §4 | SA-3 §2.1.1 → SA-2 §4.1 | ✅ |
| §3 → §4 关系 | §4.6 | SA-2 §4.6 | ✅ |
| §4 → §9 codiagnostics | §4.3 + §4.4 | SA-2 §4.3 + §9.1 | ✅ |
| §5 → §6 | §5.1 + §5.4 → §6 | SA-4 §5.1 + §5.4 → §6 | ✅ |
| §6 → §7 | §6.1 + §6.3 → §7.1 + §7.3 | SA-4 §6.1 → §7.1 | ✅ |
| §7 → §8 | §7.2 + §7.3 → §8.1 + §8.2 | SA-4 §7.2 → §8.1 | ✅ |
| §9 codiagnostics 实施 | §9.3 + §9.4 | SA-2 §9.3 + SA-4 §9.1 | ✅ |
| §10 Open Q → 5 个 SA | §10.1 全表 | SA-1 §3 + SA-4 §10.1 + SA-5 §11 | ✅ |
| §11 迁移 → §3 + §4 + §7 + §9 | §11.2 + §11.3 | SA-5 §11 → SA-2 §3/§4/§9 + SA-4 §7 | ✅ |
| §12 AsyncDemo2 → §2 + §7 | §12.2 + §12.3 | SA-5 §12 → SA-3 §2 + SA-4 §7 | ✅ |
| §2 用法 引用 §8 错误传播 | §2.5 契约 3 → §8.2 | SA-3 §2.5 → SA-2 §3.5 → SA-4 §8.2 | ✅ |

### 19 KD 编号一致性

- KD-1 至 KD-19 全部出现在 SA-1 ~ SA-5 报告中,主文档 §13.2 引用 100% 一致。
- KD-11 至 KD-19 由 SA-4 + SA-5 报告提出,主文档 §5.5 / §6.1 / §7.3 / §7.6 / §11.5 / §11.6 / §11.4 / §11.3 全部 verbatim 引用。
- 没有"KD 编号错位"或"主文档漏掉某条 KD"的情况。

### 7 个 Open Q 状态一致性

- SA-1 §3 报告的 7 个 Open Q 全部在主文档 §10.1 + §10.2 列出。
- 主会话 2026-07-30 拍板状态正确:Q1(Q4/Q5/Q6)已拍,Q2/Q3 待协调,Q7 P6+。

### 整合规则遵循(brief §"整合时的合并/去重规则")

| 规则 | 实施情况 |
|---|---|
| 1. §3 + §4 不重复 — SA-2 §3 + §4 → SA-6 §3 + §4,直接合并 | ✅ |
| 2. §5 + §6 + §7 + §8 不重复 — SA-4 4 节直接搬到 SA-6 | ✅ |
| 3. §9 合并 — SA-2 §9(契约)+ SA-4 §9(实施细节)合成 SA-6 §9 | ✅ |
| 4. §10 合并 Open Q — SA-1 §3 7 条 + SA-4 §10.1(Q1 决定)+ SA-5 §11(Q4/Q5/Q6) | ✅ |
| 5. §11 + §12 保留 — SA-5 直接搬 | ✅ |
| 6. §13 新写 — SA-6 自己写跨 spec 引用与依赖的最终汇总表 | ✅ |
| 7. §14 新写 — 修订历史 | ✅ |

### 整合时的一致性补充

- **§2.4.3 for/while 循环 await**:SA-3 §2.4.3 原文写"留给 SA-4 拍板",SA-4 §7.3 由主会话 2026-07-30 拍板"P5 全支持"。SA-6 整合时**更新** §2.4.3 为"P5 解禁"+"决策来源 §7.3",并在 §2.4.3 表中列出 7 种支持形态。这是主会话已决定项的整合,**不**改动 SA-3 子 spec(子 spec 保留原文)。
- **§8.4 / §2.4 循环 await 表**:主文档把 SA-4 §7.3 拍的"for/while 循环 await 全支持"和 SA-1 §2 非目标 3(cancellation / timeout)+ 非目标 4(try/catch)整合到 §8.4 对外可见行为表,与 §2.4 退化场景相互一致。

---

## 主会话审阅入口

- **主文档**:`Docs/superpowers/specs/2026-07-30-async-p5.md`(3193 行,14 节)
- **子 spec 目录**(分章节档案):
  - SA-1 §1 + §2 + §3:`2026-07-30-async-p5-1-goals-and-open-questions.md`
  - SA-2 §3 + §4 + §9:`2026-07-30-async-p5-2-low-level-contract.md`
  - SA-3 §2:`2026-07-30-async-p5-3-awaitable-usage-rules.md`
  - SA-4 §5-§12:`2026-07-30-async-p5-4-codegen-and-state-machine.md`
  - SA-5 §11 + §12 + §13:`2026-07-30-async-p5-5-migration-and-demo2.md`
- **笔记**:`Docs/superpowers/specs/2026-07-29-async-p5-discussion-note.md`(573 行,D1-D10 决策记录)
- **协调目录**:`.superpowers/sdd/2026-07-30-async-p5/index.md`
- **SA 报告目录**:`.superpowers/sdd/2026-07-30-async-p5/reports/sa-{1,2,3,4,5,6}-report.md`

---

## 主会话审阅检查清单(brief 给的清单)

- [x] 主文档 14 节结构齐全(§1-§12 + §13 跨 spec 引用 + §14 修订历史)— 3193 行
- [x] 19 个 KD 全部 verbatim 引用正确(对照 §13.2 + 上方 KD 汇总表)
- [x] 7 个 Open Q 状态正确(Q1/Q4/Q5/Q6 已拍,Q2/Q3 待协调,Q7 P6+— 对照 §10.1 + §13.3)
- [x] 跨 spec 引用一致(SA-1 ~ SA-5 互相引用的章节号 + KD 号都对得上,对照 §13.4)
- [x] 主文档章节顺序符合"先目标后实施"逻辑(对照 §13.5)
- [x] 5 个子 spec 作为分章节档案保留(独立 spec 不动;见 `Docs/superpowers/specs/2026-07-30-async-p5-{1,2,3,4,5}-*.md`)

---

## 主会话待审阅清单

SA-6 整合过程中识别的"主会话需关注或拍板"项:

1. **Open Q 2(clang minor 版本矩阵)** — 待主会话协调,CI matrix 落地时定。
2. **Open Q 3(Windows libtooling MT vs MD)** — 待 Windows 构建 owner 协调,SA-4 实施 PR 期间拍。
3. **KD-17 AsyncDemo 并存期具体值** — 已拍"1-2 release",具体 release 编号待主会话拍。
4. **KD-16 P4 v1 共存期截止日期** — 已拍"共存期 = 0",具体截止日期推荐 2026 Q4 之前(主会话与产品对齐)。
5. **AsyncDemo2 vs AsyncDemo CMake 集成** — 顶层 `CMakeLists.txt` 是否在 `add_subdirectory(Examples)` 下加 AsyncDemo2(主会话决定)。
6. **`MHeaderTool --migrate` 子命令的优先级** — 高优先级(SA-5 提议),主会话决定实施 PR 顺序。

---

## 与 SA-1 ~ SA-5 报告的关系

| SA 报告 | 输出 spec | 主文档整合章节 | 备注 |
|---|---|---|---|
| SA-1 | `2026-07-30-async-p5-1-goals-and-open-questions.md`(~500 行) | §1 | 全部 verbatim 整合 |
| SA-2 | `2026-07-30-async-p5-2-low-level-contract.md`(697 行) | §3 + §4 + §9 | 全部 verbatim 整合 |
| SA-3 | `2026-07-30-async-p5-3-awaitable-usage-rules.md`(487 行) | §2 | 整合时把 §2.4.3 循环 await 改成"P5 解禁"(主会话 2026-07-30 拍) |
| SA-4 | `2026-07-30-async-p5-4-codegen-and-state-machine.md`(882 行) | §5 + §6 + §7 + §8 | 全部 verbatim 整合;§5.5 合并 §10.1 缓存策略 |
| SA-5 | `2026-07-30-async-p5-5-migration-and-demo2.md`(1095 行) | §11 + §12 | 全部 verbatim 整合;§10 合并 Open Q 状态 |

---

## 实施路径建议(SA-6 提议)

主会话审阅通过后,建议落地 PR 顺序:

1. **PR 1**(高优先级):SA-5 §11.4 `MHeaderTool --migrate` 子命令 + SA-4 §5 libtooling 集成(Linux,1-2 周)
2. **PR 2**:SA-4 §6-§8 AST → IR + 状态机代码生成 + SA-2 §3-§4 底层契约(MAsync.h 新增 SAwaiter + TAwaitable)
3. **PR 3**:SA-5 §12 AsyncDemo2 新增 + AsyncDemo 走路径 B(注释化)
4. **PR 4**(Windows 路径):SA-4 §5.2 Windows libtooling 集成(3-4 周)
5. **PR 5**(CI + 多平台):Open Q 2/3 clang minor 矩阵 + Windows MT/MD

---

## 主会话触发的承诺修整(2026-07-30)

> 来源:用户 2026-07-30 在 SA-6 整合完成后,指出 SA-6 整合的 3 处表述过头 / 漏声明,需修订主文档 `2026-07-30-async-p5.md`。**触发对齐点**:C# `GetAwaiter()` 协议层 — P5 协议层(`SFutureResult::AsAwaiter()`)对齐到同一模式。

### 修订 1 — §1 目标 1 表述收紧

**用户 2026-07-30 戳出**:`"业务代码像 C# async 一样写"` 表述过头 — C# 能 await 任何实现 `GetAwaiter()` 的类型,但 P5 协议层(`SFutureResult::AsAwaiter`)也是同样的协议,**项目没实现 socket / file / timer / ThreadPool 这些类型**。

**修订**:把 §1 目标 1 改成更精确的表述:"**P5 业务代码能 await 任何返回 `SFutureResult<T>` 的表达式**,包括 `MFUNCTION(Async)` 函数 / `SFutureResult` 变量 / 项目内部基础设施方法(如 `MRpcChannel::CallToActor`,已返回 `SFutureResult<TRpcResponse>`)。系统级 socket / file / timer / ThreadPool / SyncContext **留 P6+**(需要项目先实现这些类型 + 它们的 `AsAwaiter` 协议)。"

**影响章节**:§1 目标 1 整段改写。

### 修订 2 — §4 TAwaitable 模板加澄清(新增 KDC-1)

**用户 2026-07-30 戳出**:F 不限于 `MFUNCTION(Async)` 函数,任何返回 `SFutureResult<T>` 的函数 / 类型都可作 F(只要 codegen 能在 AST 看到返回类型)。但 R 必须从 F 的返回类型推。

**修订**:§4.1 模板签名表加一行说明 + §4.3 R 推导规则加形式 C 推导段落 + 新增 **KDC-1**(`TAwaitable<F, R, Args...>` 通用化)。

**新增 KD:KDC-1**(已加入 §13.2 KD 总表):

| 字段 | 内容 |
|---|---|
| **编号** | KDC-1 |
| **锁定内容** | `TAwaitable<F, R, Args...>` 通用化:F 不限于 `MFUNCTION(Async)` 函数,**任何返回 `SFutureResult<R>` 的函数 / 方法 / 类型** 都可作 F(codegen 通过 AST `getReturnType()` 推 R,不需要反射宏元数据);`R` 总是从 F 的返回类型推 |
| **来源** | 主会话 2026-07-30(对齐 C# `GetAwaiter()` 协议层) |
| **主文档位置** | §2.1.3 + §4.1 + §4.3 |
| **来源章节** | 主会话 2026-07-30 戳出 + 用户原话"F 不限于 MFUNCTION(Async) 函数" |

**影响章节**:§4.1 模板签名表 + §4.2 三形态联合(形式 C) + §4.3 R 推导规则(形式 C 推导)+ §13.2 KD 总表(KDC-1 新增)。

### 修订 3 — §2.1 全形式清单加 形式 C — 基础设施 awaitable

**用户 2026-07-30 戳出**:§2.1 全形式清单只有 形式 A + 形式 B,缺**形式 C — 基础设施 awaitable**:`TAwaitable<AwaitableType, R>(args...)`,`AwaitableType` 是返回 `SFutureResult<R>` 的任何函数/方法,不必标 `MFUNCTION(Async)`。这是 P5 协议层的通用形式。

**修订**:§2.1.3 新增形式 C 完整段落(适用场景 + 字段语义 + R 推导 + 与形式 A 对照表 + 业务代码三形态示例)。

**连带影响**(必须同步否则内部矛盾):

| 章节 | 旧表述 | 新表述 |
|---|---|---|
| §2.1.4 #8 | "`MFUNCTION(Async)` 之外的函数(`ServerCall` / `ClientCall` / 普通函数)出现在 `TAwaitable<>` 内" 触发 类别 A | "**非 `SFutureResult<R>` 返回的函数**(同步普通函数 / `ServerCall` 老 transport 路径等)出现在 `TAwaitable<>` 内" 触发 类别 A |
| §9.1 类别 A | "包装了同步函数"`SyncFunc` 返回值非 `SFutureResult<R>`(或不带 `MFUNCTION(Async)`)" | "包装了非 `SFutureResult<R>` 返回的函数"`SyncFunc` 返回值非 `SFutureResult<R>`(同步函数 / `int` / `void` / 自定义类型)" |
| §9.2.1 示例 | "error: 'SyncAdd' is not an async function" | "error: 'SyncAdd' is not awaitable — return type is not 'SFutureResult<T>'" |
| §9.4 emit 点 A | "`SyncFunc` 无 `MFUNCTION(Async)`" | "`SyncFunc` 返回类型非 `SFutureResult<R>`(同步函数 / `int` / `void` / 自定义类型)" |

**影响章节**:§2.1(§2.1.1 + §2.1.2 编号不变,新增 §2.1.3 形式 C,原 §2.1.3 改名为 §2.1.4)+ §9.1 + §9.2.1 + §9.4 + §14 修订历史(新增 v1.1 + §14.3 主会话触发的承诺修整节)。

### 修订落地总览

主文档 `2026-07-30-async-p5.md` 落地的修改:

1. **§1 目标 1 整段改写**(修订 1)— 从"业务代码像 C# async 一样写"改为"P5 业务代码能 await 任何返回 `SFutureResult<T>` 的表达式(协议层统一)"
2. **§2.1 全形式清单** — 重新编号 + 新增 §2.1.3 形式 C + §2.1.4 #8 放宽(修订 3)
3. **§4.1 模板签名表** + **§4.2 三形态联合** + **§4.3 R 推导规则**(修订 2)— 加 F 不限于 `MFUNCTION(Async)` 的澄清 + 形式 C 推导段落
4. **§9.1 类别 A** + **§9.2.1 示例** + **§9.4 emit 点 A** — 同步对齐(连带影响)
5. **§13.2 KD 总表** — 新增 KDC-1
6. **§14 修订历史** — 新增 v1.1 + §14.3 主会话触发的承诺修整节(完整记录 3 处修订 + KDC-1)

### 子 spec 影响声明

**5 个 SA 子 spec 文件本身不动** — 子 spec 作为"分章节档案"保留原样(Similarly to how the brief specifies "子 spec 5 个独立保留不动,只写主文档")。SA-3 子 spec 中 `MRpcChannel::CallToActor` 等项目基础设施方法实例需要在后续 PR 实施时补充到 §2 / §4 示例;SA-4 codegen 主体需在 §6.2 / §7 / §9 codiagnostics 实施时考虑形式 C 的 AST getReturnType() 路径。

### 协议层对齐 — C# GetAwaiter 与 P5 AsAwaiter 对照(更新后)

| 维度 | C# | P5 |
|---|---|---|
| 协议标准 | `Task<T>.GetAwaiter()` / `ValueTask<T>.GetAwaiter()` / 自定义 awaiter | `SFutureResult<T>::AsAwaiter()`(§3)— **目前仅实现在 `SFutureResult<T>` 一种类型上** |
| awaitable 范围 | 任何实现 `GetAwaiter()` 的类型 | 任何返回 `SFutureResult<T>` 的表达式(协议层已就位;**未实现** socket / file / timer 等系统的 `AsAwaiter`) |
| 协议扩展 | 新类型实现 `GetAwaiter()` | 新类型实现 `AsAwaiter()`(同一模式) |
| 当前落地 | .NET BCL 大量类型已实现(`Stream.ReadAsync` / `Socket.ConnectAsync` / `Task.Delay` 等) | **仅** `SFutureResult<T>`(项目内部实现) |

**P6+ 推进路径**:为系统级类型实现 `AsAwaiter()` 协议(`SSocket::AsAwaiter()` / `SFile::AsAwaiter()` / `STimer::AsAwaiter()` / `SThreadPool::AsAwaiter()` / `MSyncContext::AsAwaiter()` 等)。**协议层完全通用** — `AwaitReady` / `AwaitSuspend<Frame*>` / `AwaitResume` 三方法(§3.2)已就位,新增类型只需实现三方法,**不需要新加 KD / 不需要改 §3 / 不需要改 codegen**。

---

## 修订历史

| 日期 | 说明 |
|---|---|
| 2026-07-30 | v0:SA-6 报告起草;主文档合成完成(3193 行,14 节);19 KD verbatim 引用;7 Open Q 状态锁定;5 个子 spec 保留;DONE |
| 2026-07-30 | v0.1:主会话触发的承诺修整(3 处修订 + KDC-1,详见上方"主会话触发的承诺修整"节);§1 目标 1 收紧到"P5 协议层 awaitable = 任何返回 `SFutureResult<T>` 的表达式";§2.1 加形式 C — 基础设施 awaitable;§4.1/§4.3 加澄清 — F 不限于 `MFUNCTION(Async)` 函数;§9.1 类别 A + §9.2.1 示例 + §9.4 emit 点同步对齐;新增 KDC-1 |
