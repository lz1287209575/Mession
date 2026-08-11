# TODO

当前 PoC 待办。与 `CLAUDE.md`、本仓库实现对齐。

**更新时间基线：2026-07-26**（main @ log 模块 + coding-style C1 + client-protocol step-1/2 + MClientTargetResolver 已合入之后 + P0 async cleanup 已合入）

---

## 当前已稳定

- **拓扑**：`MServiceRegistry`(:18000) + `GatewayServer`(:8001) + 同质多进程 `EchoService`(:7001/:7002)；历史六服业务代码已删除，**不恢复**
- **服务发现**：`MEndpointCache` + Registry 注册/心跳/EndpointChange；静态 `--peers` 不再是主模型
- **日志**：`Source/Common/Runtime/Log` 新 **MLog** 管道（ring / dispatcher / sinks）；`LogTest` 目标可用；旧 `Logger.h` 已删
- **反射**：`MCLASS` / `MSTRUCT` / `MPROPERTY` / `MFUNCTION` → `Build/Generated/`
- **RPC**：`MRpcChannel` + 稳定 FunctionId；client-protocol step-1/2（去掉 `EClientMessageType` / `ForwardedClientCall`，单 FunctionId 路径）
- **对象**：`MObject` + `TSharedPtr` + `IDisposable`；Actor 平面寻址 `MActorRouter`
- **风格 C1**：`Docs/CodingStyle.md` + `.clang-format` + `scripts/check-style.sh`（ABI 黑名单）已合 main
- **P0 异步清理（2026-07-25）**：`CMAKE_CXX_STANDARD 17` + 编译器显式 `-std=c++17`；`MFUTURE(T)` → `SFutureResult<T>`（7 个源文件）；`MFUTURE` 宏已删；doxygen 示例中 `co_await`/`co_return` 已改为指向 `Docs/superpowers/specs/2026-07-24-cpp17-async-await.md` 父 spec；`FiberAwait` 标注 legacy。规范：`Docs/superpowers/specs/2026-07-25-cpp17-p0-cleanup.md`。注：`LogSinks.h:59` 的 `TSpan`/`std::span` C++20 依赖是已知 follow-up，不在 P0 范围。
- **文档入口**：`CLAUDE.md` 已按 PoC 拓扑重写

---

## 当前最优先

### 1. 恢复 / 钉死 validate 绿基线

`Scripts/validate.py` 在近期 main 上曾出现 **chain timeout**（log 合入前后对照亦红，属链路/基线问题，勿当“日志回归”草率 revert）。

目标：

- 三链可重复通过：本机 Actor / 跨 Echo Actor / 未知 Actor 错误路径
- 失败时 `Logs/validate/*.log` 与 MLog 输出足以定位（注意 Inline 消息截断，长错误可能只见前缀）
- 明确 Registry 就绪窗口：Echo 注册完成后再让 Gateway 打业务包

### 2. 在 Registry 语义下证明跨进程 Actor 路由

旧方案「对端连接后 `RegisterRemoteActors` 控制面」属于 **Peers 静态时代**，**不要默认复活**。

目标：

- Echo 启动 `RegisterLocal` 上报的 `ActorIds` 经 Registry 推送后，对端/`Gateway` 的 `MEndpointCache` + `MActorRouter`/`CallToActor` 能命中 2001 等远端 Actor
- 文档与代码一致：跨进程依赖 **发现 + 懒连接**，不是启动期写死 peer 列表
- 若仍缺「ActorId → ServerType」全局表，在 SD 层补齐设计，而不是加第二套控制面

### 3. 落实 MClientManifest 真生成

现状：`MClientManifest` 生成侧仍偏 stub；Gateway 查表路径已有，表可能为空。

目标：

- `MHeaderTool` 扫描 `MFUNCTION(Client)` / `MFUNCTION(ClientCall)` / 约定的 CallClient 标签，emit `Build/Generated/MClientManifest.mgenerated.*`
- Gateway **只**经 `MClientManifest::FindByFunctionId` 路由；禁止把临时硬编码 Echo 固化成架构
- worktree `clientmanifest-emit` 中的 `MEchoClient` + `FunctionParser` 改动可作为候选，需 rebase 到当前 main 再合

### 4. 接完 server→client 推送骨架

已合入：`MClientTargetResolver` + `MClientTargetContextGuard`。

目标：

- 连接上线/下线：`RegisterConn` / `UnregisterConn`
- 处理请求期间：`MClientTargetContextGuard` 绑定当前连接
- 至少一个 `CallClient` / Notify 样例端到端（可先 Echo 公告类）

---

## 第二优先级

### 5. 协议与样例消息

- 在 `Protocol/Messages/` 补少量真实形状消息（战斗/场景/下行 Notify 各一亦可）
- `SendToClient` / downlink 有可跑脚本或 validate 断言

### 6. Coding-style C2–C5

见 `Docs/CodingStyle.md` §落地进度：Common / Servers / Tools+Protocol 全量 format + CLAUDE 快查收口。  
**单独 PR**，勿与功能改动混提。

### 7. 运行时基础 / C++17 异步（设计已定）

- **规范**：`Docs/superpowers/specs/2026-07-24-cpp17-async-await.md`（`SFutureResult` + `MFUNCTION(Async)` + `AWAIT` 状态机 + 薄 `MAsyncContext`；废弃 `MFUTURE` 包装；Fiber/`MAwait` 非主路径）
- 实施分期见该 spec §14（P0 口径/CMake17 → P1 dispatch pending → P2 垂直切片 → P3 MHeaderTool）
- 拆分超大 `ReflectionPropertyTemplates.inl`（JSON / Binary / CLI）

### 8. 文档与仓库卫生

- 过期 worktree / 已合分支清理（名实不符的 `improve-service-discovery` 挂载等）
  - 2026-08-11:删除 W1/W2/W3/W4(clientmanifest-emit / debug-baseline-timeout / improve-service-discovery / clienttarget-resolver);详见 `Docs/superpowers/specs/2026-08-11-repo-step1-worktree-cleanup-design.md`。后续候选:`mheadercodegen-ast` 独立重构、`refactor/base-project-structure`、`worktree-mheadertool-refactor`、远端 `origin/worktree-improve-service-discovery` 删除。
  - 2026-08-11:删除 `refactor/base-project-structure` 与 `worktree-mheadertool-refactor`(均已合入 main,本地无 worktree 挂载)。同步:忽略 `.opencode/`、防 core.* 重提交;`docs/` → `Docs/` 归一(5 文件已迁)。
- 统一 `Docs/` vs `docs/` 路径（log design 当前在小写 `docs/`）— 2026-08-11 完成(见上)
- 根目录与各 worktree 旧 `TODO` 副本以 **本文件 + main** 为准

---

## 不在本周期内

- 恢复 Login/World/Scene/Router/Mgo 业务树或 Player 对象树
- K8s / Registry HA / mTLS
- 大规模运维脚本替换（`server_cluster.py` 等）——等 PoC 三链稳定
- 持久化子系统作为主路径暴露

---

## 推荐施工顺序

```text
① validate 绿基线
② Registry 语义下跨 Echo CallToActor
③ MClientManifest emit + Gateway 纯查表
④ TargetResolver 接线 + CallClient 样例
⑤ 协议样例 / style C2–C5 / fiber / 文档收尾
```

冲突时：**以代码 + `CLAUDE.md` + 本文件为准**；`Docs/RefactorArchitectureAndRpc.md` 中标注为历史的 World/Peers 叙事不要当施工图。

---

## 已知 bug 记录

### MHeaderTool namespace-level enum 被误判为 nested

**现象**:`Source/Tools/MHeaderTool/Parsing/EnumParser.h:257` 的 `IsNestedEnum` 只向后找 `class / struct / public: / private:` 关键字,不识别 `namespace` 作为 enclosing scope。所以 namespace 级 enum(如 `mession::script::EScriptLanguage`、现有的 `EServiceRegistryResult` 等)被误判为 nested,emit 出返回 `nullptr` 的 no-op 注册函数,**反射系统查不到这些 enum 的 value 列表**。

**影响**:`MObject::FindEnum("EScriptLanguage")` 返回 `nullptr`,反射侧序列化 enum 时拿不到 value 表。代码侧用 enum 类型作为 `MFUNCTION` 参数仍然可以编译,但运行时反射查不到 enum value。

**触发条件**:`enum class Foo : uint8 { ... }` 出现在 namespace 内(不是 class 内、不是顶层全局)。

**修法**:改 `EnumParser.h:257` 的 `IsNestedEnum`,让它也识别 namespace scope(向上扫描 `namespace X {` 时增加 bracket 计数,确认 enum 是否在 namespace 内)。修完后 namespace 级 enum 正常反射。

**绕过**:在 MHeaderTool 修好之前,这些 enum 在反射侧查不到,但**不阻断业务**:业务侧用 enum 类型声明 `MFUNCTION` 参数仍然编译通过,只是反射查表得 fallback 到硬编码。

**下次触及**:MMO 业务动态枚举(表情包状态、动画状态、装备稀有度等)接入时,会需要这些 enum 的反射序列化,届时一并修 MHeaderTool。

---

## Script Engine abstract layer 已落地

2026-08-04:`Source/Common/Script/Abstract/{EScriptLanguage,EReloadMode,EReloadResult,SScriptEngineConfig,TVariant,ScriptErrorCodes,IScriptModule,IScriptEngine,IScriptRepl,ScriptBridge}.h` + `TVariant.cpp` + Tests。详见 `Docs/superpowers/plans/2026-08-04-script-engine-abstract.md` 与 ledger `.superpowers/sdd/script-engine-abstract-impl/progress.md`。
