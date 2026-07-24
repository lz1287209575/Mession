# TODO

当前 PoC 待办。与 `CLAUDE.md`、本仓库实现对齐。

**更新时间基线：2026-07-24**（main @ log 模块 + coding-style C1 + client-protocol step-1/2 + MClientTargetResolver 已合入之后）

---

## 当前已稳定

- **拓扑**：`MServiceRegistry`(:18000) + `GatewayServer`(:8001) + 同质多进程 `EchoService`(:7001/:7002)；历史六服业务代码已删除，**不恢复**
- **服务发现**：`MEndpointCache` + Registry 注册/心跳/EndpointChange；静态 `--peers` 不再是主模型
- **日志**：`Source/Common/Runtime/Log` 新 **MLog** 管道（ring / dispatcher / sinks）；`LogTest` 目标可用；旧 `Logger.h` 已删
- **反射**：`MCLASS` / `MSTRUCT` / `MPROPERTY` / `MFUNCTION` → `Build/Generated/`
- **RPC**：`MRpcChannel` + 稳定 FunctionId；client-protocol step-1/2（去掉 `EClientMessageType` / `ForwardedClientCall`，单 FunctionId 路径）
- **对象**：`MObject` + `TSharedPtr` + `IDisposable`；Actor 平面寻址 `MActorRouter`
- **风格 C1**：`Docs/CodingStyle.md` + `.clang-format` + `scripts/check-style.sh`（ABI 黑名单）已合 main
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
- 统一 `Docs/` vs `docs/` 路径（log design 当前在小写 `docs/`）
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
