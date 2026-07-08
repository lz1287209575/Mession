# Mession 基建重构 — 架构与 RPC 系统设计

> 起草：2026-06-12
> 状态：v1（待审）
> 范围：基建层（Common 框架、6 Server 骨架、跨服 RPC、UE 客户端接入路径）。
> 不含业务实现（Combat/Player/Scene 业务内容）。

---

## 0. 一页摘要

| 维度 | 决策 |
|------|------|
| 服务形态 | **同质多进程**——每个进程是 1 个 SampleService；6 服是历史命名，PoC 阶段合并为 1 个 SampleService 类 |
| 寻址 | **Service.Instance 平面化**——`ActorId`（uint64）→ 进程 / Service |
| 跨服 RPC | **只 ObjectCall**——`MRpcChannel::CallToActor(ActorId, Class, Method, Req) → MFUTURE(TResp)` |
| 客户端接入 | **Gateway 单一入口**——UE → Gateway(8001) → World(8003) → SampleService(Actor) |
| 客户端下行 | **`MRpcChannel::SendToClient(Conn, Class, Method, Resp)`**——唯一出口，必须经 Gateway |
| 代码生成 | **保留 MHeaderTool**——`MFUNCTION(ServerCall/Client/ClientCall/Async/PlayerRPC/...)` 继续驱动反射 |
| 协程 | **`MFUTURE(T) = SFutureResult<T>`**，内部用 `MFuture<TResult<T, FAppError>>` |
| PoC 范围 | 2 个 SampleService 进程（A、B），演示 Actor 迁移；1 个 Gateway + 1 个 World |

---

## 1. 设计目标与约束

### 1.1 目标

1. **同质多进程架构**——6 服不是最终形态；每个进程都是同一种逻辑服务，差异在启动参数决定的 Service 名
2. **跨服 RPC 走唯一一条路径**（ObjectCall）—— 客户端→Server→对象，2 层语义
3. **Service.Instance 平面化寻址**——`ActorId`（uint64）定位，不存在 Player 树状结构
4. **保留 MHeaderTool**——`MFUNCTION` 宏继续驱动 ServerCall 路由、参数反射、客户端下行生成
5. **UE 客户端接入路径明确**——`MRpcChannel::SendToClient` 单一出口

### 1.2 非目标

- 不实现业务（Combat/Player/Scene 的具体内容）
- 不重写 6 服——PoC 阶段合并为 1 个 SampleService 类
- 不做 K8s 部署、监控、告警等运维侧设计

### 1.3 关键约束

- **新基建优先**：用户已落地的 `MActorRouter` / `MRpcChannel` / `MServerCallProxyBase` / `MFUTURE(T)` 是设计核心，不倒退
- **MHeaderTool 不可删**：`Client/ServerCall/ClientCall/Async/PlayerRPC` 等 transport 标签已就位
- **服务器是同质进程**：每个进程只持一个 ServiceName；多 Service 需多进程

---

## 2. 现状基线（已落地 — 不可改）

| 组件 | 路径 | 角色 |
|------|------|------|
| `MActorRouter` | `Common/Net/Routing/ActorRouter.{h,cpp}` | 进程内 Actor（uint64）→ ServerType + ConnectionId 路由表 |
| `MRpcChannel` | `Common/Net/Rpc/MRpcChannel.{h,cpp}` | 统一 ServerCall + ClientCall 入口 |
| `IRpcTransportResolver` | `Common/Net/Rpc/RpcRuntimeContext.h` | 抽象 server→transport |
| `MServerRuntimeContext` | 同上 | 默认实现：TMap<EServerType, MServerConnection> + RegisterRpcTransport |
| `MFUTURE(T)` | `Common/Runtime/Async/MAsync.h` | SFutureResult<T> 别名 = MFuture<TResult<T, FAppError>> |
| `MServerCallProxyBase` | `Servers/App/ServerCallProxy.h` | ServerCall 代理基类 |
| `CallServerFunction<TResp>(Conn, "Class", "Method", Req)` | `Common/Net/Rpc/RpcServerCall.h` | 稳定 ID 路径：编译期 MGET_STABLE_RPC_FUNCTION_ID |
| `MFUNCTION(ServerCall)` | `MHeaderTool/.../FunctionParser.h` | 已支持 ServerCall/ClientCall/Client/Async/PlayerRPC |
| `MObjectCallRouter` | `Servers/App/ObjectCallRouter.{h,cpp}` | **旧机制**（按 EObjectCallRootType 寻址）—— **应退役** |
| `MObjectCallRegistry` | `Servers/App/ObjectCallRegistry.h` | **旧机制**（RootType → Resolver）—— **应退役** |
| 6 个 Server 骨架 | `Servers/{Gateway,Login,World,Scene,Router,Mgo}/*` | 已是空骨架：保留 Init/OnAccept/ShutdownConnections，业务 RPC 全部移除 |

### 2.1 已删除的业务（不需要恢复，重新设计）

- `World/Player/*` — Player 对象树、状态、命令运行时
- `World/WorldClient*` — UE 客户端适配层
- `World/Backend/*` — World→其它服的方法包装
- `Scene/Combat/*` — Monster、Skill、Combat Runtime
- `Login/LoginSession*` — 登录会话业务
- `Mgo/MgoPlayerState*` — 持久化业务
- `Gateway/Rpc/GatewayBackendRpc*` — Gateway 异质业务

---

## 3. 目标架构

### 3.1 总体结构

```
┌──────────────────────────────────────────────────────────┐
│ UE 客户端（自带 RpcManifest / 反射表）                    │
└────────────────────┬─────────────────────────────────────┘
                     │ EClientMessageType::MT_FunctionCall (13)
                     │ [13][FunctionId:2B][CallId:8B][Size:4B][Payload]
                     ▼
        ┌────────────────────────┐
        │     Gateway (8001)     │ 唯一对外端口
        │  - 接受 UE TCP 连接    │
        │  - 上行：MT_ClientProxy 透传到 World
        │  - 下行：MRpcChannel::SendToClient
        │  - 维护 (ConnId → ClientConnection) 表
        └────────────┬───────────┘
                     │ MT_ClientProxy(30) [0x1E][BEConnId:8B][原ClientPacket]
                     ▼
        ┌────────────────────────┐
        │     World (8003)       │ 业务编排者
        │  - 接受 ClientProxy    │
        │  - 拆出 ClientFunctionId → 找到对应 (ServiceName, ActorId)
        │  - MRpcChannel::CallToActor(ActorId, Class, Method, Req)
        │  - 接收响应后回推给 Gateway
        └────────────┬───────────┘
                     │ MRpcChannel::CallToActor
                     │ → MActorRouter::FindActor(ActorId).ServerType
                     │ → IRpcTransportResolver::ResolveServerTransport
                     │ → SendServerCallMessage
                     ▼
┌──────────────────────────────────────────────────────────┐
│  业务服务进程（同质多进程 — 启动参数决定 ServiceName）    │
│  ┌──────────────┐  ┌──────────────┐                      │
│  │ SampleSvc A  │  │ SampleSvc B  │ ...                  │
│  │ (pid 7001)   │  │ (pid 7002)   │                      │
│  │ 注册 Actor:  │  │ 注册 Actor:  │                      │
│  │  1001→Self   │  │  2001→Self   │                      │
│  │  1002→Self   │  │              │                      │
│  │              │  │              │                      │
│  │ MFUNCTION    │  │ MFUNCTION    │                      │
│  │ (ServerCall) │  │ (ServerCall) │                      │
│  │ 的 Method    │  │ 的 Method    │                      │
│  └──────────────┘  └──────────────┘                      │
└──────────────────────────────────────────────────────────┘
```

### 3.2 关键不变量

1. **同进程只能持有一个 ServiceName**——多 Service 需多进程
2. **ActorId 进程内唯一**——`MActorRouter::RegisterActor` 重复 ID 覆盖
3. **ClientCall 必须走 Gateway 出口**——任何 server 都**不直接** `Send` 到 client TCP socket，必须通过 Gateway 的 `PushClientDownlink` ServerCall
4. **跨服 ServerCall 通过 Resolver**——`MRpcChannel::Call(Resolver, ...)` 内部用 `IRpcTransportResolver::ResolveServerTransport`，**禁止**直接 `CallServerFunction(Connection, ...)` 绕过 resolver
5. **ObjectCall 寻址用 `CallToActor`**——`Call(EServerType, ...)` 仅用于"按 Service 路由、不关心实例"（PoC 阶段不暴露）

### 3.5 Object 生命周期与所有权

#### 设计目标

让上层调用方**完全不需要关心 MObject 的释放**。`NewMObject<T>(Outer, Name)` 签名保持不变（仍返回 `T*`），底层用 `TSharedPtr<MObject>` 接管默认资源；**非托管资源**通过 `IDisposable` 接口显式释放（C# 风格）。

#### 三个核心 API

| API | 签名 | 所有权语义 |
|-----|------|-----------|
| `NewMObject<T>(Outer, Name, args...)` | `T*` | 内部 `MakeShared<T>(...)`；`Outer != nullptr` 时挂到 `Outer->Children`；`Outer == nullptr` 时 `AddToRoot()` |
| `CreateDefaultSubObject<T>(Owner, ...)` | 同上 + `MarkAsDefaultSubObject` | 同上 |
| ~~`DestroyMObject`~~ | **删除** —— 不再暴露 | — |

**为什么删 `DestroyMObject`**：底层引用计数自动回收，上层不需要触发释放的 API。如果业务需要"立刻释放"，把它的所有 `TSharedPtr` 引用全部 reset 即可（`RemoveFromRoot()` 或从 Owner Children 数组移除）。

#### IDisposable 接口（C# 风格）

```cpp
class IDisposable {
public:
    virtual ~IDisposable() = default;

    // 业务层主动释放非托管资源；必须是幂等的
    virtual void Dispose() = 0;

    // 接口默认提供，外部不要重写
protected:
    void MarkDisposed() { bDisposed = true; }
    bool IsDisposed() const { return bDisposed; }
private:
    bool bDisposed = false;
};
```

**语义**：
- `Dispose()` 是**业务层主动释放非托管资源**的入口
- **调用时机**：
  1. **业务层显式调用**（推荐）：Actor 主动下线、连接关闭、服务停止时调 `Dispose()`
  2. **MObject 析构时兜底**：`~MObject` 检测 `dynamic_cast<IDisposable*>(this)`，若 `!bDisposed` 自动调一次（防止业务忘了）
- **`Dispose()` 必须幂等**：第二次调 no-op

**适用场景**：

| 场景 | 举例 |
|------|------|
| 持有非托管资源 | TCP 连接、文件句柄、DB 连接、GPU 资源、Lua state |
| 注册了回调需反注册 | `MActorRouter::UnregisterActor`、事件订阅、计时器 |
| 启动了子线程/定时器 | 定时 Tick、Async loop |
| 外部系统对象句柄 | 第三方库注册的对象、跨进程引用 |

**不适用场景**（不需要实现 IDisposable）：
- 纯数据对象（只有 `MPROPERTY`）
- 不持有外部资源的 Service（如 `MRouterRegistry` 只持 `TMap`）

#### Dispose 传播：非链式

- Parent::Dispose() **不**主动调 Children::Dispose()
- Children 在自己 `~MObject()` 析构时，**若**是 IDisposable 且 `!bDisposed` 才兜底调一次
- **理由**：Owner 主动 Dispose 通常是"我要下线"，但 Owner 不该负责 Children 释放的细节；Children 自己析构时再释放更自然
- **业务模式**：Service Shutdown 时**不**递归 Dispose 所有子对象（让 shared_ptr 析构链触发）；只在业务层明确"我先释放某某资源"时调 `Dispose()`

#### Outer 链 = 强根（UE 风格）

- `MObject::Children`：`TArray<TSharedPtr<MObject>>`
- `MObject::Outer`：`MObject*` raw 指针（避免循环）
- `SetOuter(newOuter)`：
  - 旧 Outer `RemoveChildObject(this)`（Children erase 对应 TSharedPtr → 引用 -1）
  - 新 Outer `AddChildObject(this)`（Children 追加 TSharedPtr → 引用 +1）
- **释放时序**：Outer 析构 → `Children.clear()` → 每个 TSharedPtr 析构 → 引用为 0 的 Child `delete` → 递归
- **每个 Child 析构时**：先 `~MObject()`（内含 IDisposable 兜底），再走 MObject 默认清理

#### Root 集（无 Outer 的对象）

- `MObject::GetRootSet()`：`TSet<TSharedPtr<MObject>>`
- `AddToRoot()`：把 TSharedPtr 插入 Root 集
- 进程退出前 main 返回前清空一次，触发所有 Root 对象 `delete`

#### 构造时强制检测循环 Outer 引用

`NewMObject<T>(Outer, Name, ...)` 内部：
1. `check(Outer != this)` — 不能自己 Outer 自己
2. `check(!HasOuterChainContains(Outer, this))` — 从 `Outer` 上行 walk Outer 链，若任何祖先 `== this`，**已形成环**（A 是 B 的祖先，把 B Outer 给 A 会成环）
3. 违反 LOG_FATAL（不可恢复）

#### 调用方约定

- **不要**手动 `delete` MObject（如果非托管资源，用 `Dispose()`）
- **不要**写循环 Outer 引用（构造时 assert 兜底）
- **不要**在 `MObject` 析构函数里访问 `Outer` 或 `Children`（可能已部分释放）
- **持有 MObject** 用 raw `T*` 指针（依赖 Owner 存活）；**不持有仅观察** 用 `TWeakPtr<MObject>`
- **实现 IDisposable**：仅当持有非托管资源；`Dispose()` 必须幂等
- **触发 Dispose 的两种方式**：业务主动 / MObject 析构兜底（双保险）
- **不要**在 `Dispose()` 里调 `delete this` 或 `RemoveFromRoot()`（shared_ptr 会处理）

#### 与 `MActorRouter` 的关系

- `MObject` 管内存
- `MActorRouter` 管 `ActorId → ServerType` 路由
- **二者完全解耦** —— 通过 Service 进程内显式协调

Actor 实例**可以**是 MObject（需要 `MPROPERTY` 域标记时），**也可以**是 plain struct（不需要反射时）。`MActorRouter` 不强制 MObject 化。

#### 旧代码改造点

| 调用点 | 现状 | 改造 |
|--------|------|------|
| `if (!X) X = NewMObject<...>(this, ...)` | WorldServer InitServices 等 | 保留（`NewMObject` 允许多次同名创建） |
| `DestroyMObject(Object)` | PlayerManager/MonsterManager 有 | **删除调用点** |
| 析构里释放 socket/file/db | 当前散落 | 集中到 `Dispose()` |
| 循环 Outer 引用 | 旧 Player 树有风险 | 构造时 assert |
| 持有 MObject raw ptr | Server 多数 | 不变（依赖 Owner 存活） |

### 3.3 寻址语义对比

| 维度 | 旧（已删） | 新（设计） |
|------|------------|------------|
| 寻址维度 | `EObjectCallRootType` + RootId + ObjectPath（树） | `ServiceName` + `ActorId`（平面） |
| 跨服定位 | `MObjectCallRegistry::ResolveOwnerServerType`（注册表） | `MActorRouter::FindActor(ActorId).ServerType`（动态路由） |
| 实例注册 | `MPlayerManager` 创建 Player 时注册 | Service 启动时 `MActorRouter::RegisterActor` 批量 |
| 服务端查找 | `MObjectCall::MDetail::ResolveLocalTargetObject`（子树遍历） | Service 内部 `TMap<ActorId, ServiceInstance*>` O(1) |
| 客户端调用 | `WorldClient::Client_*`（每服薄壳） | Gateway → ClientProxy → World → CallToActor |
| 客户端下行 | `WorldServer::QueueClientNotify` → `Gateway::PushClientDownlink` | `MRpcChannel::SendToClient(Connection, Class, Method, Response)`（**统一**） |

---

## 4. 跨服 RPC 系统设计

### 4.1 三种调用语义（不重叠）

| 名称 | 入口 | 用途 | 寻址 |
|------|------|------|------|
| **ClientCall** | `MRpcChannel::SendToClient(Conn, Class, Method, Resp)` | Server → Client | 固定 ConnId（UE 物理连接） |
| **ServerCall（PoC 不暴露）** | `MRpcChannel::Call(Resolver, EServerType, Class, Method, Req)` | Server A → Server B，**不关心实例** | EServerType 粗粒度路由 |
| **ObjectCall** | `MRpcChannel::CallToActor(Resolver, ActorId, Class, Method, Req)` | Server A → Server B 上的某个 Service 实例 | ActorId 精确寻址 |

**ObjectCall 内部流程**（`MActorRouter::SendToActor`）：
1. `MActorRouter::FindActor(ActorId)` → `SActorRoute{ ActorId, ServerType, ConnectionId }`
2. `Resolver->ResolveServerTransport(Route.ServerType)` → `MServerConnection`
3. `CallServerFunction<TResp>(Connection, TargetClass, FunctionName, Request)` → `MFUTURE(TResp)`

### 4.2 协议层（不修改）

- **客户端上行**：`[0x0D][FunctionId:2B][CallId:8B][Size:4B][Payload]`
- **客户端下行**：`[0x0D][FunctionId:2B][Size:4B][Payload]`（无 CallId，UE 端走回调）
- **ServerCall 请求**：`MT_FunctionCall(0x1C)[FunctionId:2B][CallId:8B][Size:4B][Payload]`
- **ServerCall 响应**：`MT_FunctionResponse(0x1D)[FunctionId:2B][CallId:8B][bSuccess:1B][Size:4B][Payload]`
- **ClientProxy 转发**：`MT_ClientProxy(0x1E)[BEConnId:8B][原ClientPacket]`
- **稳定 ID 机制**：`MGET_STABLE_RPC_FUNCTION_ID(ClassName, MethodName)` 编译期生成

### 4.3 错误传播

- **底层**：`MFUTURE(T) = MFuture<TResult<T, FAppError>>`
- **FAppError**：`{ Code: MString, Message: MString }`
- **`Get()` 失败时抛 `FFutureResultError`**（`Common/Runtime/Async/MAsync.h:30`）
- **`GetResult()` 失败时返回原始 `TResult`**（不抛）
- **错误日志约定**：每个 ServerCall 调用方都应 `.Then([](MFuture<...> C) { if (C.Get().IsErr()) LOG_WARN(...); })` 或在协程中 try/catch

---

## 5. PoC 设计（2 链 SampleService）

### 5.1 PoC 目标

证明以下链路**端到端跳通**：
1. **链 1（UE→SampleServiceA）**：测试脚本发送 MT_FunctionCall(13) 到 Gateway(8001) → MT_ClientProxy(30) → World(8003) → 拆 ClientFunctionId → MRpcChannel::CallToActor(ActorId=1001, "MSampleService", "Echo", Req) → SampleSvc A → 回包给 UE
2. **链 2（SampleServiceA→SampleServiceB）**：SampleSvc A 收到 Echo 后内部 `MRpcChannel::CallToActor(ActorId=2001, "MSampleService", "Echo", Req)` → 跨进程跳到 SampleSvc B → B 回包 → A 收包 → A 拼装最终响应回 UE

### 5.2 进程布局

| 进程 | 端口 | ServiceName | 启动参数 | 注册 Actor | 暴露 Method |
|------|------|-------------|----------|------------|-------------|
| Gateway | 8001 | — | `--listen=8001 --world=127.0.0.1:8003` | — | `PushClientDownlink`(已有) |
| World | 8003 | — | `--listen=8003 --gateways=[]` | — | `DispatchClientCall`(新)、`DispatchObjectCall`(新) |
| SampleSvc A | 7001 | `MSampleService` | `--listen=7001 --service=MSampleService --peers=127.0.0.1:8003,127.0.0.1:7002 --actors=1001,1002` | 1001, 1002 | `Echo`(新, MFUNCTION ServerCall) |
| SampleSvc B | 7002 | `MSampleService` | `--listen=7002 --service=MSampleService --peers=127.0.0.1:8003,127.0.0.1:7001 --actors=2001,2002` | 2001, 2002 | `Echo`(新, MFUNCTION ServerCall) |

### 5.3 关键代码点

#### 5.3.1 SampleService（合并后）

```cpp
// Source/Servers/SampleService/SampleService.h
MCLASS(Type=Service)
class MSampleService : public MNetServerBase, public MObject, public MServerRuntimeContext
{
    MGENERATED_BODY(MSampleService, MObject, 0)
public:
    bool Init(int InPort = 0);
    void Tick();
    uint16 GetListenPort() const override;
    void OnAccept(uint64 ConnId, TSharedPtr<INetConnection> Conn) override;
    void ShutdownConnections() override;
    void OnRunStarted() override;

    // Actor-based RPC
    MFUNCTION(ServerCall)
    MFuture<TResult<FSampleEchoResponse, FAppError>> Echo(const FSampleEchoRequest& Req);

private:
    void RegisterActors(const TVector<uint64>& ActorIds);
    TSharedPtr<MServerConnection> WorldConn;

    TMap<uint64, TSharedPtr<FSampleActorState>> Actors;
};
```

#### 5.3.2 World 端 DispatchClientCall

```cpp
// Source/Servers/World/WorldServer.cpp（伪代码）
MFUTURE(FForwardedClientCallResponse) MWorldServer::DispatchClientCall(
    const FForwardedClientCallRequest& Request)
{
    // 1. 解析 ClientFunctionId → (ServiceName, MethodName)（读 ClientManifest）
    // 2. 提取 Request.Payload 中的 ActorId（约定在 payload 头部 8 字节）
    // 3. MRpcChannel::CallToActor(GetRpcTransportResolver(),
    //                               ActorId,
    //                               ServiceName, MethodName,
    //                               PayloadTail)
    // 4. 响应 → BuildClientFunctionPacket(FunctionId, Result.Payload, Packet)
    //         → 通过 Gateway::PushClientDownlink
}
```

#### 5.3.3 测试脚本

```python
# Scripts/poc_sample_service.py
# 启动 Gateway + World + SampleSvcA + SampleSvcB
# 用 socket 发 MT_FunctionCall(13) 到 Gateway:8001
# 期望：收到 ClientCall 响应，payload 含 "B→A→UE"
```

### 5.4 PoC 验收标准

| 编号 | 验收项 | 通过条件 |
|------|--------|----------|
| AC-1 | Gateway 启动 | 监听 8001，5s 内接受 TCP |
| AC-2 | World 启动 | 监听 8003，5s 内接受 TCP |
| AC-3 | SampleSvcA/B 启动 | 监听 7001/7002，5s 内接受 TCP |
| AC-4 | 跨服连接建立 | A→World, B→World 双向 Connected，Actor 注册到 MActorRouter |
| AC-5 | 链 1 跳通 | UE 模拟包 → A.Echo → A 回包，UE 收到 |
| AC-6 | 链 2 跳通 | A.Echo 内部 B.Echo → B 回包 → A 拼装回包，UE 收到 "B→A→UE" |
| AC-7 | 错误传播 | 故意调不存在的 ActorId（9999），UE 收到 "actor_not_found" 错误 |
| AC-8 | 协程风格 | A.Echo 用 MFUTURE 而非裸 callback |

---

## 6. 不一致点与需要修改/删除的文件

### 6.1 必须修改

| 文件 | 改动 | 原因 |
|------|------|------|
| `Source/Servers/World/WorldServer.cpp` | `DispatchClientCall` 从 `not_implemented` 改为解析 ClientManifest + CallToActor | 当前 stub 无业务 |
| `Source/Servers/World/WorldServer.cpp` | `HandleGatewayPacket` MT_ClientProxy 分支：拆出 ConnId + ClientPacket，**直接调本对象的 `DispatchClientCall`（同步路径）**或塞入协程 | 当前直接改首字节为 MT_FunctionCall 路径错 |
| `Source/Servers/World/WorldServer.cpp` | 删除 `FPlayerRootResolverStub`（我之前加的，方向错） | 跟 Service.Instance 平面化冲突 |
| `Source/Servers/World/WorldServer.cpp` | 保留 `ObjectCallRouter` 但标记为 deprecated；新建 `MObjectCallDispatcher` 走 CallToActor | 不一次性砍旧机制 |
| `Source/Servers/World/WorldServer.cpp` | `QueueClientNotify` 改用 `MRpcChannel::SendToClient(World→Gateway Connection, ...)`，但**实际推送仍经 Gateway** | 简化下行路径 |
| `Source/Servers/World/WorldServer.cpp` | 删除 Login/Scene/Mgo 单独连接，统一用 `RegisterRpcTransport(EServerType::AnyService, ...)` | 6 服合并 |

### 6.2 必须删除（旧机制退役）

| 文件 | 删除原因 |
|------|----------|
| `Source/Servers/App/ObjectCallRouter.{h,cpp}` | 用 MActorRouter 替代 |
| `Source/Servers/App/ObjectCallRegistry.{h,cpp}` | 用 MActorRouter 替代 |
| `Source/Servers/App/ObjectCall.h` | 同上 |
| `Source/Servers/App/MRouterRegistry.h`（空文件） | 同上 |
| `Source/Servers/World/WorldClient*.{h,cpp}` | 已删，无需动 |

### 6.3 PoC 阶段新增

| 文件 | 作用 |
|------|------|
| `Source/Servers/SampleService/SampleService.{h,cpp}` | 合并后的 SampleService（替代 Login/Scene/Router/Mgo 4 服） |
| `Source/Servers/SampleService/SampleServiceMain.cpp` | main 入口，解析启动参数决定 ServiceName + ActorIds |
| `Source/Servers/SampleService/SampleServiceEcho.{h,cpp}` | Echo RPC 实现 + 测试 Actor 状态 |
| `Source/Protocol/Messages/SampleService/FSampleEchoMessages.h` | FSampleEchoRequest/Response |
| `Source/Protocol/Messages/Common/ClientFunctionRoute.h` | ClientFunctionId → (ServiceName, MethodName) 路由表（ClientManifest 替代） |
| `Scripts/poc_sample_service.py` | 启动 + 测试脚本 |
| `CMakeLists.txt` | 新增 SampleService target；删除 4 个旧 Server target（Login/Scene/Router/Mgo） |

### 6.4 不修改（保留现状）

| 组件 | 原因 |
|------|------|
| `Common/Net/Routing/ActorRouter.{h,cpp}` | 已是新基建核心 |
| `Common/Net/Rpc/MRpcChannel.{h,cpp}` | 已是新基建核心 |
| `Common/Net/Rpc/RpcServerCall.h` + `.cpp` | CallServerFunction 走稳定 ID，无需改 |
| `Common/Runtime/Async/MAsync.h` | MFUTURE 已就位 |
| `MHeaderTool/.../*` | 反射宏已支持所有 transport |
| `Servers/Gateway/GatewayServer.{h,cpp}` | 已经是"透传 + 维护连接表"骨架，仅做小幅对齐 |

---

## 7. 风险与缓解

| 风险 | 影响 | 缓解 |
|------|------|------|
| **MObjectCallRouter 删除**会破坏 6 服启动（WorldServer `Init` 中 `NewMObject<MObjectCallRouter>`） | World 启动失败 | 删前先把 WorldServer.cpp 改成不依赖 |
| **`MClientManifest.generated.h` 当前是空 stub**（`FindByFunctionId` 返回 nullptr） | ClientFunctionId → 路由表查不到 | PoC 阶段手写一个 `ClientFunctionRoute.h`，存 2-3 个测试路由 |
| **MActorRouter 跨进程不同步** | 进程 B 不知道 Actor X 在 A 上 | PoC 阶段假设启动时静态配置；后续接 Router/控制面服务 |
| **MFUNCTION(ServerCall) 在不同进程的 MSampleService 上生成相同 FunctionId** | 反射 ID 冲突 | MHeaderTool 已用 `(ClassName, MethodName)` 哈希，**应该不会冲突**；PoC 阶段验证 |
| **World 单独一个进程可能成为热点** | 单点失败 | PoC 阶段接受；后续设计 World 集群 |

---

## 8. 验收里程碑

- [ ] **M1**：本文档通过审阅
- [ ] **M2**：删除 MObjectCallRouter/Registry 相关旧代码，WorldServer 启动通过
- [ ] **M3**：新增 SampleService 进程，Echo 单机跳通
- [ ] **M4**：新增 World::DispatchClientCall 实现 ClientManifest 查表
- [ ] **M5**：测试脚本证明链 1 + 链 2 跳通，AC-1~AC-8 全过
- [ ] **M6**：把 Login/Scene/Router/Mgo 4 个 Server target 从 CMakeLists 移除

---

## 附录 A：术语表

- **Actor**——个 uint64 标识的逻辑实体；在 PoC 阶段对应一个 SampleService 实例
- **ActorId**——Actor 的 uint64 ID；在 MActorRouter 中作为 key
- **Service**——个进程提供的能力集合；同质多进程下每个进程就是一个 Service
- **ServiceName**——个进程的逻辑名；在 PoC 阶段所有 SampleService 进程都叫 `MSampleService`
- **ObjectCall**——按 ActorId 寻址的跨服 RPC
- **ServerCall**——按 EServerType 寻址的跨服 RPC（PoC 不暴露）
- **ClientCall**——Server → Client 的下行调用，必须经 Gateway
- **Resolver**——`IRpcTransportResolver`，把 EServerType 映射到 MServerConnection

## 附录 B：现状 / 目标 / 改动文件 一览表

| 状态 | 类别 | 文件 |
|------|------|------|
| 已落地（不改） | 新基建 | `Common/Net/Routing/ActorRouter.*` |
| 已落地（不改） | 新基建 | `Common/Net/Rpc/MRpcChannel.*` |
| 已落地（不改） | 新基建 | `Common/Net/Rpc/RpcRuntimeContext.h` |
| 已落地（不改） | 新基建 | `Common/Net/Rpc/RpcServerCall.*` |
| 已落地（不改） | 新基建 | `Common/Runtime/Async/MAsync.h` |
| 已落地（不改） | 新基建 | `Servers/App/ServerCallProxy.h` |
| 已落地（不改） | 代码生成 | `MHeaderTool/...` |
| 已落地（保留） | 骨架 | `Servers/Gateway/GatewayServer.*` |
| 已落地（保留） | 骨架 | `Servers/Login/LoginServer.*`、`Servers/Scene/*`、`Servers/Router/*`、`Servers/Mgo/*`（PoC 阶段合并后删除） |
| 已落地（修改） | 编排 | `Servers/World/WorldServer.*`（DispatchClientCall 改实现） |
| 旧机制（删除） | 退役 | `Servers/App/ObjectCallRouter.*`、`ObjectCallRegistry.*`、`ObjectCall.h` |
| 新增 | PoC | `Servers/SampleService/*`、`Protocol/Messages/SampleService/*`、`Scripts/poc_sample_service.py` |
