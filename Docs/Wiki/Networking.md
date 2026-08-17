# Networking — 网络层与服务发现

> 浓缩自已落地实现（2026-07 actor-RPC 重构 + 2026-07-13 服务发现）。
> 本页只描述**当前代码里已经实现**的能力；设计过程中的讨论、未落地部分不在此列。

---

## 1. 总览

同质多进程架构：一个 C++ binary + 一个 ServiceName 决定进程角色。当前 PoC 有 3 种进程：

| 进程 | 启动参数示例 | 职责 |
|---|---|---|
| `MServiceRegistry` | `--listen=18000` | 集中服务发现，纯内存（不持久化） |
| `EchoService` | `--registry=127.0.0.1:18000 --listen=17001 --inst=1 --actors=1001 --local-type=Echo` | 业务 Service，注册/心跳/懒连接 |
| `Gateway` | `--registry=127.0.0.1:18000 --listen=18001 --local-type=Gateway` | 客户端接入，向业务 Service 转发 |

跨进程调用统一走 `MRpcChannel`（ServerCall 反射链路）；Service 之间的 endpoint 由 `MServiceRegistry` 下发、`MEndpointCache` 缓存并懒建连接。

---

## 2. ActorId 布局与寻址

### 2.1 ActorId 布局（`MServiceId`）

定义于 `Source/Servers/App/ServiceId.h`。ActorId 是 64 位无符号整数，**平面化寻址** `(ServiceId, InstId)`：

```
[ServiceId: 高 32 位][InstId: 低 32 位]
```

- 高 32 位 = ServiceId = `EServerType` 数值（`Echo = 7` 等）。
- 低 32 位 = 实例号 InstId。
- 常用 API：`MServiceId::Make(EServerType, InstId)` 构造；`GetServiceType / GetServiceId / GetInstId` 提取字段。

### 2.2 MActorRouter（进程内路由表）

定义于 `Source/Common/Net/Routing/ActorRouter.h/.cpp`，进程级单例 `MActorRouter::Get()`。

- **注册**：`RegisterActor(uint64 ActorId, EServerType ServerType, uint64 ConnectionId = 0)`；本机 actor 注册时标记 `EServerType::Unknown`，作为 `IsActorLocal` 的判定依据。
- **查询**：`FindActor` 返回 `SActorRoute`；`IsActorLocal` 判断目标是否在本进程；`UnregisterActor / UpdateActorRoute` 维护路由。
- **发送**（模板）：
  - `SendToActor<TResponse>(ActorId, ClassName, FunctionName, Request, DefaultServerType = Unknown)`：查路由 → 路由未命中时用 `DefaultServerType` → `MEndpointCache::GetOrConnect(TargetServerType)` 拿连接 → `MObject::FindClass` → `CallServerFunction`。
  - `RouteToActor(ActorId, Call)`：调用方自提供"拿到 connection 后如何调"的逻辑。
- actor 对象表（`IActor*` 与 Handle）委托给 `MActorSystem` 单例，路由表由 `MActorRouter` 自己维护，避免双份所有权。

### 2.3 跨进程寻址

跨实例寻址依赖服务发现把远端 actor 注册进本机路由表：

- 每个 Service 启动时把本地 `LocalActorIds` 注册到 Registry（`MakeLocalEndpoint` 把 InstId 转成 64 位 ActorId）。
- Registry 推 `EndpointChange` 时，`MEndpointCache::OnEndpointChange` 会把各远端 endpoint 携带的 `ActorIds` 注册进 `MActorRouter`（`ServerType` = 实际服务类型）——这样 `EchoA` 收到 `EchoB` 的推送后，`FindActor(2001)` 才能命中并据此选择连接。
- 只注册"未存在"的 actor：本进程已注册的本地 actor（`Unknown`）不会被端点信息覆盖。
- 局限：PoC 同主机 loopback（`Address="0.0.0.0"`），跨主机不在当前实现范围。

---

## 3. MRpcChannel（统一 RPC 通道）

定义于 `Source/Common/Net/Rpc/MRpcChannel.h/.cpp`，单例 `MRpcChannel::Get()`。通道自身不再持有 transport 解析器——**连接选择统一交给 `MEndpointCache`**。

| 方法 | 用途 | 路径 |
|---|---|---|
| `Call<TResponse>(EServerType, ClassName, MethodName, Request)` | 服务器间 ServerCall | `GetOrConnect(TargetServer)` → `FindClass` → `CallServerFunction` |
| `CallToActor<TResponse>(ActorId, ClassName, MethodName, Request, DefaultServer = Unknown)` | 按 ActorId 寻址的 ServerCall（唯一 ObjectCall 入口） | 转发给 `MActorRouter::SendToActor` |
| `SendToClient(Connection, ClassName, MethodName, Response)` | 客户端下行 | 按反射 `FunctionId` 发包 |
| `CallActor / CallActorAndWait` | 跨进程 actor 消息（`FActorMessage`，actor-extension） | 序列化进 wire 后经 ServerCall 投递 |

返回值统一为 `SFutureResult<T>`（即 `MFuture<TResult<T, FAppError>>`），调用方用 `.Then(...)` 或 `AWAIT_OK` 消费；错误以 `FAppError` 传播（如 `connection_unavailable`、`class_not_found`、`server_call_timeout`）。

---

## 4. 服务发现

### 4.1 Registry 进程（MServiceRegistry）

`Source/Servers/ServiceRegistry/`。纯内存，`--listen=18000` 默认端口；`SServiceRegistryConfig` 心跳阈值可配：

| 配置 | 默认值 | 语义 |
|---|---|---|
| `HeartbeatTimeoutMs` | 15000 | 超过此时间未心跳 → `bHealthy = false` + 推 `EndpointChange` |
| `EvictAfterMs` | 30000 | 超过此时间未心跳 → 从 `Endpoints_` 移除 + 推 `EndpointChange` |

内部结构：`Endpoints_`（by ServerId）、`Sessions_`（by ConnId，记录每个连接的注册态）、`MonotonicSeq`（per-ServerType 推送序号）。

行为要点：
- **Register**：重复 ServerId 且未注册 → DEBUG `LOG_FATAL` / RELEASE 拒绝（`Ack` 带 `AlreadyExists`）；成功后 `Ack` + 推 `EndpointChange`。
- **Heartbeat**：续约 `LastHeartbeatMs`；若此前 unhealthy 翻转回 healthy 也推 `EndpointChange`。
- **ListEndpoints**：返回过滤后的 healthy endpoint 列表，带递增 `Seq`（用 `EndpointChange` 包回）。
- **UpdateActors**：更新该实例的 `ActorIds` 并推 `EndpointChange`。
- **断连**：已注册的连接断开 → 移除 endpoint + 推 `EndpointChange`。
- `TickHeartbeats` 由 `TickBackends` 每帧驱动。

### 4.2 Registry 协议

`Source/Common/Net/ServiceDiscovery/Endpoint.h` + `RegistryProtocol.h`。复用 `MServerConnection` 的 TCP frame（1 字节 type + payload），`EServiceRegistryMessageType` base 200 起跳，避开 `EServerMessageType` 的 `MT_RPC=27 / MT_FunctionCall=28 / MT_FunctionResponse=29`：

| 枚举 | 值 | 方向 | Payload |
|---|---|---|---|
| `Register` | 200 | C→S | `FServiceEndpoint` |
| `Deregister` | 201 | C→S | `uint32 ServerId` |
| `Heartbeat` | 202 | C→S | `ServerId, TimestampMs` |
| `UpdateActors` | 203 | C→S | `ServerId, TVector<uint64>` |
| `ListEndpoints` | 204 | C→S | `EServerType` |
| `EndpointChange` | 205 | S→C | `Type, Seq, TVector<FServiceEndpoint>` |
| `Ack` | 206 | 双向 | `EServiceRegistryResult, Message` |

`FServiceEndpoint`：`ServerType / ServerId / Address("0.0.0.0") / Port / LastHeartbeatMs / bHealthy / ActorIds`。注册时由 `MakeLocalEndpoint<TConfig>(Config)` 从 Service config 构造。

### 4.3 MEndpointCache（Service 侧）

`Source/Common/Net/ServiceDiscovery/EndpointCache.h/.cpp`，进程级单例。生命周期由 `Service::Init` 编排：

```
AttachEventLoop(&EventLoop)  →  BindRegistry(host, port)  →  RegisterLocal(SelfEndpoint)
→ 每帧 Tick(DeltaTime)（Gateway 0.1f / Echo 0.1f）  →  关停 DeregisterAndShutdown()
```

- **`GetOrConnect(TargetServerType)`**：查 `Endpoints_[Type]` → 空则告警返 `nullptr` → round-robin 选 healthy endpoint（跳过本进程自己的 `ServerId`，防自连）→ 查 `ConnectionPool`（by ServerId）命中且在线则复用 → 否则 `LazyConnect` 建连入池 → 全部失败返 `nullptr`。带 `SubId` 的 per-Sub 重载为多 Reactor 扩展（各自独立的池与锁）。
- **`LazyConnect(Ep)`**：按 `Ep.ServerType` 解析 endpoint class（`GetServerEndpointClassName`），用 `127.0.0.1 + Ep.Port` 建 `MServerConnection`，并 `AttachDispatchToConnection` 挂入站分发（`MT_FunctionCall → DispatchBackendServerCallPacket`、`MT_FunctionResponse → HandleServerCallResponse`）。
- **`OnEndpointChange(Type, Seq, NewEndpoints)`**：`Seq` 单调校验（旧推送 DEBUG `LOG_FATAL` / RELEASE 拒绝覆盖）→ 覆盖 `Endpoints[Type]` → 把新 endpoint 的 `ActorIds` 注册进 `MActorRouter` → `PurgeDisappeared` 清理已断开的池连接（保留仍活跃的连接供 LivenessProbe 自愈）。
- **心跳与重连**：连上 Registry 后每 5s 发一次 `Heartbeat`（`FRegistryClient::HeartbeatTimer`）；断线置 `bConnected=false` 并 5s 重试 `TryConnectRegistry`，重连成功后重发 `Register`（若已有 LocalInfo）并补发 `ListEndpoints`。
- **启动期主动拉全量**：`RegisterLocal` 时对业务 ServerType（Gateway/Echo）发 `ListEndpoints`，后续被动接收 `EndpointChange` 增量更新。

### 4.4 DEBUG vs RELEASE 失败分级

`MLogIsDebugBuild()`（基于 `NDEBUG`）：

| 场景 | DEBUG | RELEASE |
|---|---|---|
| Registry 连接失败 | `LOG_FATAL` 退出（迭代快） | `LOG_ERROR` + `Endpoints_` 保持空 + 后台 5s 重连，`GetOrConnect` 返 `nullptr`（调用方走 RPC 错误/重试） |
| `RegisterLocal` 被拒 | `LOG_FATAL` | `LOG_ERROR` + 继续降级运行 |
| 旧 Seq 推送 | `LOG_FATAL` | `LOG_WARN` 拒绝覆盖 |
| `GetOrConnect` 空表/全 unhealthy | `LOG_WARN` | `LOG_DEBUG`（避免失败风暴刷屏） |

---

## 5. ServerCall 反射 RPC 链路

核心实现：`Source/Common/Net/Rpc/RpcServerCall.h/.cpp`、`RpcTransport.h/.cpp`、`RpcManifest.h/.cpp`。

### 5.1 包格式

- **TCP frame**（`MServerConnection::SendPacket`）：`[1B PacketType][payload]`；服务器间类型 `EServerMessageType`：`MT_FunctionCall=28`（请求）、`MT_FunctionResponse=29`（响应）。
- **ServerCall 请求**（`BuildServerCallPacket`）：`[FunctionId:2B][CallId:8B][PayloadSize:4B][Payload]`。
- **ServerCall 响应**（`BuildServerCallResponsePacket`）：`[FunctionId:2B][CallId:8B][Success:1B][PayloadSize:4B][Payload]`。
- 请求/响应 payload 均由反射序列化（`BuildPayload / ParsePayload`，`MSTRUCT` 消息）。

### 5.2 出站调用

`MRpcChannel::Call / CallToActor` → `MEndpointCache::GetOrConnect` 取连接 → `FindServerCallFunctionByName(TargetClass, MethodName)`（限定 `Transport == "ServerCall"`）→ `RegisterServerCall(Completion, 5.0s, LivenessProbe)` 登记 pending call → `BuildServerCallPacket` → `SendServerCallMessage(Connection)`（`MT_FunctionCall`）。

### 5.3 入站分发

连接收到 `MT_FunctionCall` 后：

1. `DispatchBackendServerCallPacket(ServiceInstance, Connection, Data)`（或 Inbound 版）解析出 `FunctionId / CallId / Payload`；
2. 构造 `MServerCallResponseTarget`（回调里 `BuildServerCallResponsePacket` + `SendServerCallResponseMessage` 沿原连接回包）；
3. `DispatchServerCall(TargetInstance, FunctionId, CallId, Payload, ResponseTarget)`：`FindServerCallFunctionById`（限定 ServerCall）→ 调 `Function->ServerCallHandler(TargetInstance, Payload)`，返回 `SFutureResult<TByteArray>`；
   - Ready 路径：同步发回成功/错误响应；
   - Pending 路径：`.Then` 经 `MAsyncContext::Post` 回包。

对端收到 `MT_FunctionResponse` → `HandleServerCallResponse` → `ConsumeServerCall(CallId, Response)` → resolve 对应 Promise。

### 5.4 稳定函数 ID

`MGET_STABLE_RPC_FUNCTION_ID(ClassName, MethodName)` 展开为 `GetStableRpcFunctionIdByName`，即 `ComputeStableReflectId(ClassName, MethodName)`——由 `(ClassName, MethodName)` 字符串稳定哈希生成 `uint16` FunctionId，保证不同进程对同一 ServerCall 生成一致的 ID。`MFUNCTION(ServerCall)` 反射宏生成 `ServerCallHandler` 与元数据（`MClass::FindFunctionById` 查 ID → 调 handler）。

### 5.5 超时与错误

`PumpServerCallMaintenance` 扫描 pending calls：
- 超过 5s 默认超时 → `server_call_timeout`；
- `LivenessProbe`（连接 `IsConnected()` 弱引用探活）失败 → `server_call_disconnected`。

### 5.6 已落地调用链

- **链 1（Gateway → Echo）**：Gateway 在 `GetOrConnect(EServerType::Echo)` 取到 EchoService 连接后发 ServerCall；Echo 侧 `OnAccept` 入站分发到 `MEchoService::Echo`（`Source/Servers/EchoService/EchoService.cpp`）。
- **链 2（EchoA → EchoB 跨进程转发）**：`MEchoService::Echo` 解析 `TargetActorId` 的 ServiceType/InstId；目标在本机（`IsActorLocal`）则本地直返；否则 `MRpcChannel::CallToActor(TargetActorId, "MEchoService", "Echo", Request)` → `MActorRouter` 命中远端路由（由 `EndpointChange` 注册）→ `GetOrConnect(Echo)` 懒连 → 反射到 EchoB 的 `Echo`。
- 跨进程 actor 消息（`CallActor / CallActorAndWait`）接收端在 `MEchoService::OnActorMessage / OnActorCall`，投递到 `MActorSystem::DispatchLocal` 后由 actor 自己的 Sub 线程处理。

---

## 6. 相关实现（关键文件）

| 文件 | 内容 |
|---|---|
| `Source/Servers/App/ServiceId.h` | ActorId 布局与 `MServiceId` 工具 |
| `Source/Common/Net/Routing/ActorRouter.h/.cpp` | `MActorRouter` 路由表 + `SendToActor / RouteToActor` |
| `Source/Common/Net/Rpc/MRpcChannel.h/.cpp` | `MRpcChannel::Call / CallToActor / SendToClient / CallActor*` |
| `Source/Common/Net/Rpc/RpcServerCall.h/.cpp` | `DispatchServerCall`、`CallServerFunction`、pending-call 注册/超时 |
| `Source/Common/Net/Rpc/RpcTransport.h/.cpp` | ServerCall/响应包编解码、`SendServerCallMessage` |
| `Source/Common/Net/Rpc/RpcManifest.h/.cpp` | ServerType↔class 绑定、`GetServerEndpointClassName` |
| `Source/Common/Net/ServiceDiscovery/Endpoint.h/.cpp` | `FServiceEndpoint`、`EServiceRegistryMessageType`、`MakeLocalEndpoint` |
| `Source/Common/Net/ServiceDiscovery/RegistryProtocol.h/.cpp` | Registry 各消息的打包/解析 |
| `Source/Common/Net/ServiceDiscovery/EndpointCache.h/.cpp` | `MEndpointCache`：GetOrConnect / OnEndpointChange / 心跳 / LazyConnect |
| `Source/Servers/ServiceRegistry/ServiceRegistryServer.h/.cpp` | Registry 进程：注册/心跳/超时 evict/推送 |
| `Source/Servers/ServiceRegistry/ServiceRegistryConfig.h` | `--listen / --heartbeat-timeout-ms / --evict-after-ms` |
| `Source/Servers/EchoService/EchoService.h/.cpp` | 业务示例：本地直返 + 跨进程 `CallToActor` 转发 |
| `Source/Servers/Gateway/GatewayServer.h/.cpp` | 客户端接入 + 经 `MEndpointCache` 寻址业务 Service |
| `Source/Common/Net/ServerConnection.h/.cpp` | TCP 连接、`SendPacket` frame、心跳、重连 |
| `Source/Common/Runtime/Reflect/ReflectionArchiveInvoke.inl` | 稳定函数 ID（`MGET_STABLE_RPC_FUNCTION_ID`） |
| `Source/Servers/App/ServiceMain.h` | Service 进程启动框架（`MServiceMain::Run<TService, TConfig>`） |
