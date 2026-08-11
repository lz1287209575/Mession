# Mession 基建 — 架构与 RPC（现状 + 演进）

> 原稿起草：2026-06-12（World 中间层 + 静态 peers 时代）  
> **修订：2026-07-24** — 对齐当前 main：Registry 发现、无 World 编排、client-protocol FunctionId 路径、MLog  
> 范围：基建与 PoC 拓扑。不含具体玩法业务实现。  
> **施工入口以 `CLAUDE.md` / `Scripts/servers.py` / 本文件「现状」章为准。**

---

## 0. 一页摘要（2026-07 现状）

| 维度 | 决策（当前） |
|------|----------------|
| 服务形态 | **同质多进程**业务 worker（`EchoService`）+ 独立 **Gateway** + 独立 **MServiceRegistry** |
| 寻址 | **ActorId（uint64）平面化** → 本机 `MActorRouter` / 远端经发现与 `CallToActor` |
| 服务发现 | **MServiceRegistry + MEndpointCache**（注册 / 心跳 / EndpointChange / 懒连接）；**禁止**把静态 `--peers` 当主模型 |
| 跨服 RPC | **`MRpcChannel::CallToActor`（ObjectCall）** 为主；按 `EServerType` 的粗粒度 Call 为辅 |
| 客户端接入 | **Gateway 唯一对外端口**；UE/validate → Gateway → 后端 Service（**无 World 进程**） |
| 客户端协议 | **稳定 FunctionId 单路径**（step-1/2 已合）；legacy `EClientMessageType` / `ForwardedClientCall` 已删 |
| 客户端下行 | 经 Gateway（如 `PushClientDownlink`）；目标连接框架：`MClientTargetResolver` |
| 代码生成 | **保留 MHeaderTool**；`MClientManifest` **真 emit 仍是缺口** |
| 日志 | **MLog** 异步管道（`Common/Runtime/Log`）；旧 `Logger` 已移除 |
| 异步 | **合同 `SFutureResult<T>`**；目标路径见 `Docs/superpowers/specs/2026-07-24-cpp17-async-await.md`（`Async`/`AWAIT` 状态机 + `MAsyncContext`；C++17；Fiber 非主路径）。现状 handler 多为同步完成 future |

### 0.1 历史设计（已废弃作施工图）

| 旧决策 | 状态 |
|--------|------|
| Gateway → **World(8003)** → SampleService | **未采用**；编排职责收在 Gateway 路由 + Echo 业务 |
| `MT_ClientProxy` 透传 World | **已淘汰**（与 client-protocol 收口一致） |
| 启动参数静态 `--peers` 全连接 | **已替换**为 Registry |
| 六服 Login/World/Scene/Router/Mgo 业务骨架 | **已删除**，PoC 不恢复 |

---

## 1. 设计目标与约束

### 1.1 目标

1. 同质多进程：业务进程同类，差异在 `--inst` / `--actors` / Service 配置  
2. 跨服对象调用统一走 Actor 平面寻址  
3. 发现与连接生命周期集中（Registry + Cache）  
4. 客户端只认识 Gateway；服务端不直连 UE 做玩法推送  
5. 保留 MHeaderTool 驱动的反射与稳定 FunctionId  

### 1.2 非目标

- 恢复已删玩法对象树（Player/Combat/…）  
- Registry HA、跨机自动发现（K8s/DNS/etcd 另案）  
- 完整 fiber 运行时选型落地（见 TODO）  

### 1.3 关键约束

- `MActorRouter` / `MRpcChannel` / `MFUTURE` / `MEndpointCache` 是主路径，不倒退到 ObjectCallRoot 树或静态 peer 表  
- `MFUNCTION` transport 标签继续由 MHeaderTool 解析  
- 每个业务进程一种逻辑服务角色（PoC 即 Echo）  

---

## 2. 现状基线（代码已落地）

| 组件 | 路径 | 角色 |
|------|------|------|
| `MActorRouter` | `Common/Net/Routing/ActorRouter.*` | 进程内 Actor 路由；远端连接经 `MEndpointCache::GetOrConnect` |
| `MRpcChannel` | `Common/Net/Rpc/MRpcChannel.*` | 统一 Server / Client 侧 RPC 入口 |
| `MEndpointCache` | `Common/Net/ServiceDiscovery/EndpointCache.*` | Registry 客户端：缓存 endpoint、懒连接、心跳 |
| Registry 协议 / 类型 | `Common/Net/ServiceDiscovery/{Endpoint,RegistryProtocol}.*` | 注册包、推送、序列化 |
| `MServiceRegistry` | `Servers/ServiceRegistry/*` | 独立发现进程 |
| Gateway / Echo | `Servers/Gateway/*`, `Servers/EchoService/*` | 入口 + 业务 worker |
| `MService` / `MServiceMain` | `Servers/App/*` | 反射 CLI 配置、生命周期、**MLog::Init** |
| Client protocol | `Common/Net/Rpc/{RpcDispatch,RpcTransport,RpcClientCall}.*` 等 | FunctionId 路径（step-1/2） |
| `MClientTargetResolver` | `Common/Net/ClientCall/*` | server→client 目标解析框架（接线未完） |
| `MClientManifest` | `Common/Net/Rpc/MClientManifest*` | **表结构在；生成仍可能为空 stub** |
| `MLog` | `Common/Runtime/Log/*` | 异步日志 |
| `MFUTURE` | `Common/Runtime/Async/MAsync.h` | 异步结果类型 |
| `MHeaderTool` | `Source/Tools/MHeaderTool/*` | 反射与（规划中）Manifest emit |

### 2.1 已删除且不恢复

- `World/Player/*`、WorldClient、Scene/Combat 业务、Login 会话业务、Mgo 玩家状态业务等  
- 旧 `ObjectCallRouter` / `ObjectCallRegistry` 树寻址（以 Actor 平面为准）  
- 静态 peers 作为发现主路径  

---

## 3. 目标 / 现状架构

### 3.1 总体结构（当前实现）

```
┌──────────────────────────────────────────────┐
│ UE / Scripts/validate.py                     │
└────────────────────┬─────────────────────────┘
                     │ MT_FunctionCall
                     │ FunctionId + CallId + Payload
                     ▼
              ┌──────────────┐
              │ Gateway:8001 │  查 MClientManifest（待真表）
              │ EndpointCache│  GetOrConnect(EServerType)
              └──────┬───────┘
                     │ ServerCall / 业务 RPC
         ┌───────────┴───────────┐
         ▼                       ▼
  ┌─────────────┐         ┌─────────────┐
  │ Echo A:7001 │         │ Echo B:7002 │
  │ actors 1001…│         │ actors 2001…│
  └──────┬──────┘         └──────┬──────┘
         │                       │
         └───────────┬───────────┘
                     │ --registry=
                     ▼
              ┌─────────────────┐
              │ Registry:18000  │
              └─────────────────┘
```

启动顺序（`Scripts/servers.py`）：Registry → Echo×N → Gateway。

### 3.2 关键不变量

1. 客户端只连 Gateway；业务进程默认不对外暴露给 UE  
2. ActorId 在约定命名空间内可路由；本机注册进 `MActorRouter`  
3. **Client 下行必须经 Gateway**（禁止业务进程直写 UE socket 作为主路径）  
4. 跨服连接解析优先 **`MEndpointCache`**，不手写对端地址表  
5. 客户端 API 路由长期依赖 **生成 Manifest**，不依赖永久硬编码  

### 3.3 寻址语义

| 维度 | 旧（已删） | 现在 |
|------|------------|------|
| 寻址 | RootType + 对象路径树 | **ActorId 平面** |
| 发现 | 静态 peers / 手写连接 | **Registry + EndpointCache** |
| 客户端编排 | World 拆包转发 | **Gateway 查表 + 调后端** |
| 下行 | 多路径混杂 | Gateway 统一出口（推送 resolver 在补齐中） |

### 3.4 Object 生命周期与所有权

（原则未变，摘要保留。）

- 默认所有权：`TSharedPtr` / Outer 子树；**不暴露** `DestroyMObject`  
- 非托管资源：`IDisposable::Dispose()`，须幂等；析构可兜底  
- Dispose **不**强制链式调 Children；Children 随引用析构  
- `MObject` 管内存，`MActorRouter` 管 Actor 路由，二者解耦  
- Actor 实例可以是 `MObject`，也可以是轻量状态；Router 不强制 MObject  

细节与 Outer 环检测约定同既有实现（`Common/Runtime/Object`）；新增代码勿再引入手动 `delete` MObject。

---

## 4. 跨服 RPC 与客户端协议

### 4.1 调用语义

| 名称 | 入口（概念） | 用途 | 寻址 |
|------|----------------|------|------|
| **ObjectCall** | `MRpcChannel::CallToActor(...)` | 打到某 Actor 所在进程 | ActorId |
| **ServerCall（类型级）** | `Call` + `EServerType` | 不关心具体实例时 | 类型 + Cache 选实例 |
| **Client 下行** | Gateway `PushClientDownlink` / channel SendToClient 族 | Server → UE | 连接 / TargetResolver |

ObjectCall 典型步骤：

1. 解析 Actor 所在 `EServerType`（本机路由表和/或发现侧元数据）  
2. `MEndpointCache::GetOrConnect(Type)` → `MServerConnection`  
3. `CallServerFunction` / 稳定 FunctionId 发 RPC  
4. `MFUTURE` / `TResult` 回传错误  

### 4.2 协议层（PoC）

- **客户端上行**：FunctionCall 包 + **FunctionId** + CallId + Payload（具体帧布局以 `RpcTransport` / validate 为准）  
- **服间**：FunctionCall / FunctionResponse + 稳定 FunctionId  
- **已移除**：依赖 `EClientMessageType` 多类型分支、`ForwardedClientCall` 消息族、以 World 为中心的 ClientProxy 编排叙事  
- **稳定 ID**：`MGET_STABLE_RPC_FUNCTION_ID` / 名称哈希；重命名保 ID 用 `Api` / `ClientApi`  

### 4.3 错误传播

- `MFUTURE(T)` → `TResult<T, FAppError>`  
- 调用方应对 `IsErr()` / 异常路径打日志（`LOG_*` / category 宏）  
- DEBUG 下发现严重不一致可 `LOG_FATAL` / `MLogIsDebugBuild()` 分级（见 EndpointCache / Registry）  

### 4.4 服务发现（摘要）

详见 `Docs/superpowers/specs/2026-07-13-service-registry-design.md`。

- Service：`--registry=host:port`，`RegisterLocal` + 周期 Heartbeat  
- Registry：内存表，超时 unhealthy / 驱逐，向 watcher 推 `EndpointChange`  
- Cache：按类型 round-robin 健康实例、断线重连  

---

## 5. PoC 进程与验收

### 5.1 进程布局（实现）

| 进程 | 默认端口 | 关键参数 | 职责 |
|------|----------|----------|------|
| MServiceRegistry | 18000 | `--listen` | 发现 |
| EchoService A/B | 7001 / 7002 | `--registry` `--actors` `--inst` | 业务 + 本地 Actor |
| GatewayServer | 8001 | `--registry` | 客户端入口 |

另：日志相关 CLI（`--log-file` / `--log-config` / …）由各服务 Config 反射字段 + `MServiceMain` 初始化 MLog。

### 5.2 链路验收（意图）

| 编号 | 场景 | 期望 |
|------|------|------|
| AC-1 | Registry / Echo / Gateway 启动 | 端口监听；Registry 上有注册 |
| AC-2 | 链本机 Actor（如 1001） | Client→Gateway→EchoA→回包 |
| AC-3 | 链远端 Actor（如 2001） | 经发现/CallToActor 到 EchoB 再回包 |
| AC-4 | 未知 Actor | 明确错误，而非挂死 |
| AC-5 | Manifest（完成后） | 无硬编码表仍能路由 Client FunctionId |

驱动脚本：`Scripts/validate.py`、`Scripts/servers.py`（**不是**旧文中的 World 四进程脚本）。

### 5.3 已知缺口（与 `TODO.md` 一致）

1. validate 全链偶发/持续 timeout — **优先修基线**  
2. 跨 Echo 在 Registry 语义下是否完全闭环 — 需证明  
3. `MClientManifest` 真生成  
4. `MClientTargetResolver` 全路径接线 + CallClient 样例  

---

## 6. 演进对照（原稿任务清单状态）

| 原稿项 | 状态 |
|--------|------|
| 删 ObjectCall 树寻址、六服业务 | **已做** |
| SampleService / Echo 同质进程 | **已做**（名 `EchoService`） |
| World::DispatchClientCall | **不做**；职责在 Gateway |
| 静态 peers 互联 | **已替换为 Registry** |
| ClientManifest 手写/生成 | **生成仍 TODO** |
| 链 1/2 脚本验收 | **有 validate；绿性需重新钉死** |
| MLog / client-protocol step-1/2 / TargetResolver | **后增已合 main** |

### 6.1 不要再按原稿改的文件

- 不要重建 `Servers/World/*` 编排层作为 PoC 必经之路  
- 不要恢复 `ForwardedClientCall` / 多 `EClientMessageType` 客户端模型  
- 不要把 `RegisterRemoteActors` 静态互相同步当作默认架构（除非 SD 证明不足且评审另开设计）  

---

## 7. 风险与缓解

| 风险 | 缓解 |
|------|------|
| Manifest 空表导致 Gateway 无法路由 | 真 emit + 校验；过渡期明确 PoC fallback 且标 temporary |
| Registry 未就绪就打业务包 | validate/servers 启动顺序与就绪等待；Cache 重连 |
| 跨进程 Actor 元数据不全 | 用 Registry ActorIds + 推送闭环；补测试 |
| 文档与代码漂移 | `CLAUDE.md` + 本文「现状」+ `TODO.md` 同步改 |
| 日志 Inline 截断影响排障 | 排障用文件 sink / 加长策略（见 log design） |

---

## 8. 里程碑（滚动）

- [x] M1 同质 Echo + Gateway 骨架  
- [x] M2 去掉旧 ObjectCall 树 / 六服业务  
- [x] M3 Registry + EndpointCache  
- [x] M4 MLog 替换旧 Logger  
- [x] M5 client-protocol FunctionId 收口 + TargetResolver 框架  
- [ ] M6 validate 三链稳定绿  
- [ ] M7 Manifest 真生成 + Gateway 纯查表  
- [ ] M8 CallClient 端到端 + TargetResolver 接线  
- [ ] M9 风格 C2–C5 / 文档路径统一等收尾  

---

## 附录 A：术语表

- **Actor / ActorId** — 平面逻辑实体及其 uint64 id  
- **EchoService** — PoC 同质业务进程（历史文稿中的 SampleService）  
- **Registry** — `MServiceRegistry` 发现进程  
- **EndpointCache** — 进程内发现缓存与连接池  
- **ObjectCall** — 按 ActorId 的跨服调用  
- **ClientCall / CallClient** — 客户端相关 RPC / 服务端推客户端（命名以 MFUNCTION 标签为准）  
- **FunctionId** — 稳定反射/RPC 函数 id  
- **Gateway** — 唯一客户端入口  

## 附录 B：相关文档

| 文档 | 内容 |
|------|------|
| `CLAUDE.md` | 给代理/开发的仓库地图与命令 |
| `TODO.md` | 当前优先级 backlog |
| `Docs/superpowers/specs/2026-07-13-service-registry-design.md` | 发现设计 |
| `Docs/superpowers/specs/2026-07-14-log-module-design.md` | 日志设计 |
| `Docs/CodingStyle.md` | 代码风格（C1 已合；C2–C5 TODO） |
| `Docs/superpowers/specs/2026-07-07-actor-rpc-refactor.md` | Actor RPC 重构上下文 |

## 附录 C：原稿结构说明

2026-06 版中的 World 四进程图、§6 必改 WorldServer 清单、§5.3 SampleService 伪代码保留思想价值（Actor 平面、CallToActor、下行经 Gateway），**拓扑与文件清单已过时**。若需考古，请用 git history 查看本文件旧版本。
