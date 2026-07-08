# 同质多进程 + Actor-based RPC 重构 — 设计 Proposal

> 起草：2026-07-07
> 状态：v1（与 `RefactorArchitectureAndRpc.md` 对齐）
> 关联：`/root/Mession/Docs/RefactorArchitectureAndRpc.md`（权威设计）

---

## 0. 一句话

把当前 6 服异质架构，**重构为同质多进程 + Actor-based RPC**：
- 6 个 Server 进程按启动参数决定 ServiceName
- 跨服调用全部走 `MRpcChannel::CallToActor(ActorId, ...)`
- 客户端上行 → Gateway → World → 业务 Service（PoC 阶段：1 个 SampleService）
- 客户端下行 → 业务 Service → `MRpcChannel::SendToClient` → Gateway → UE
- `MObject` 底层用 `TSharedPtr` + `IDisposable`（C# 风格）做生命周期管理

## 1. 目标

1. **同质多进程**——Service 进程是 1 个 C++ binary + 1 个 ServiceName
2. **Service.Instance 平面化寻址**——`(ServiceName, ActorId)` 唯一定位
3. **跨服 RPC 唯一接口**——`MRpcChannel::CallToActor`
4. **MObject 零关心释放**——`NewMObject<T>` 签名不变，底层 shared_ptr 接管
5. **PoC 跑通 2 链**：UE→ServiceA→ServiceB（演示 Actor 路由）

## 2. 非目标

- 不恢复任何已删业务（Player/Combat/Session/Mgo/WorldClient/Backend）
- 不实现具体玩法（Combat、Player 状态、Scene 同步）
- 不做 K8s 部署、监控、告警
- 不重写 6 服（PoC 阶段合并为 1 个 SampleService）

## 3. 现状基线（详见设计文档 §2）

| 类别 | 状态 |
|------|------|
| 已落地（不动） | `MActorRouter` / `MRpcChannel` / `IRpcTransportResolver` / `MServerCallProxyBase` / `MFUTURE(T)` / MHeaderTool 反射宏 |
| 已删（不恢复） | `MPlayer*` / `MWorldClient*` / `World/Backend/*` / `Scene/Combat/*` / `LoginSession` / `MgoPlayerState` |
| 仍残留（要退役） | `MObjectCallRouter` / `MObjectCallRegistry` / `EObjectCallRootType` |
| 6 Server 骨架 | `Gateway/World/Router` 有少量内容；`Login/Scene/Mgo` 是纯空骨架 |

## 4. 目标架构（详见设计文档 §3）

```
UE ──MT_FunctionCall──> Gateway(8001)
                         │ MT_ClientProxy
                         ▼
                       World(8003) ── MRpcChannel::CallToActor ──> Service 进程
                                                                     │
                                                                     ├─> SampleService A (MSampleService)
                                                                     └─> SampleService B (MSampleService)
```

**5 条不变量**：
1. 同进程只持一个 ServiceName
2. ActorId 进程内唯一
3. ClientCall 必须经 Gateway 出口
4. 跨服 ServerCall 通过 Resolver
5. ObjectCall 寻址用 `CallToActor`

**寻址语义**：平面化 `(ServiceName, ActorId)`，无树状结构

## 5. 关键技术决策（详见设计文档 §3.5、§4）

### 5.1 MObject 生命周期

- `NewMObject<T>(Outer, Name)` 签名不变，**返回 `T*`**
- 内部用 `MakeShared<T>(...)` 持有
- `MObject::Children` 改 `TArray<TSharedPtr<MObject>>`
- **`DestroyMObject` 删除**（不再暴露）
- **新增 `IDisposable` 接口**（C# 风格）：业务层主动释放非托管资源
- `Dispose()` 幂等；`MObject` 析构时兜底调用一次
- 构造时强制 `check(Outer != this) && check(!HasOuterChainContains(Outer, this))`，循环引用 LOG_FATAL

### 5.2 RPC 系统

- `MRpcChannel::Call(Resolver, EServerType, Class, Method, Req)` — ServerCall（PoC 不暴露）
- `MRpcChannel::CallToActor(Resolver, ActorId, Class, Method, Req)` — **ObjectCall 唯一入口**
- `MRpcChannel::SendToClient(Connection, Class, Method, Response)` — 客户端下行
- 稳定 ID：`MGET_STABLE_RPC_FUNCTION_ID(ClassName, MethodName)` 编译期生成
- 错误传播：`MFUTURE(T) = MFuture<TResult<T, FAppError>>`

## 6. PoC 范围

**2 链 SampleService**：1 个 Gateway + 1 个 World + 2 个 SampleService 进程（A、B），UE 不接入，用 Python 脚本发包验证。

| 链 | 路径 |
|----|------|
| 链 1 | 脚本 → Gateway → World → ServiceA.Echo → 回包 |
| 链 2 | 脚本 → Gateway → World → ServiceA.Echo → ServiceA 内部 `CallToActor(2001)` → ServiceB.Echo → ServiceA 拼包 → 回包 |

**8 条验收标准** AC-1 ~ AC-8（详见设计文档 §5.4）

## 7. 验收里程碑（详见设计文档 §8）

| M | 描述 | 状态 |
|----|------|------|
| M1 | `RefactorArchitectureAndRpc.md` 通过审阅 | ✅ |
| M2 | 删除 `MObjectCallRouter`/`ObjectCallRegistry` 旧机制；WorldServer 启动通过 | ⏳ |
| M3 | 新增 `MSampleService` 进程；Echo 单机跳通 | ⏳ |
| M4 | `World::DispatchClientCall` 实现 ClientManifest 查表 | ⏳ |
| M5 | 测试脚本证明链 1 + 链 2 跳通（AC-1~AC-8 全过） | ⏳ |
| M6 | CMakeLists 移除 Login/Scene/Router/Mgo 4 个 target | ⏳ |

## 8. 风险（详见设计文档 §7）

| 风险 | 缓解 |
|------|------|
| `MObjectCallRouter` 删除会破坏 WorldServer `Init` | M2 阶段先改 WorldServer.cpp 再删 |
| `MClientManifest.generated.h` 当前是空 stub | PoC 阶段手写 `ClientFunctionRoute.h` |
| `MActorRouter` 跨进程不同步 | PoC 假设启动时静态配置；后续接 Router/控制面服务 |
| `MFUNCTION(ServerCall)` 在不同进程生成相同 FunctionId | MHeaderTool 已用 `(ClassName, MethodName)` 哈希，PoC 验证 |
| World 单独一个进程可能成为热点 | PoC 阶段接受；后续 World 集群 |

## 9. 与详细设计文档的关系

本 proposal 是**摘要版**。所有细节、表格、伪代码、验收标准在：
`/root/Mession/Docs/RefactorArchitectureAndRpc.md`

本文件用于：
- 评审/对齐（给 stakeholder 一页纸）
- 关联 tasks（每个 task 引用本文件的对应章节）
- 状态追踪（M1~M6）

## 10. 关联文件

- 设计：`/root/Mession/Docs/RefactorArchitectureAndRpc.md`
- 实施计划：`/root/Mession/Docs/superpowers/plans/2026-07-07-actor-rpc-refactor.md`
- 现状：用户工作树（55 个 docs 清理 + RefactorArchitectureAndRpc.md 新增 + 用户的基建变更）
