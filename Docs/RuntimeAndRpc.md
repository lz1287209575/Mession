# 运行时与 RPC

## 反射层

当前工程的反射系统基于以下宏：

- `MCLASS`
- `MSTRUCT`
- `MPROPERTY`
- `MFUNCTION`
- `MGENERATED_BODY`

它们共同支撑三类能力：

- 结构与对象快照读写
- RPC 元数据发现与分发
- 属性域标记驱动的 Persistence / Replication

`MHeaderTool` 会扫描 `Source/` 下使用这些宏的头文件，生成反射 glue code 到 `Build/Generated/`。

### 请求校验元数据

`MPROPERTY` 现在不仅承载序列化和属性域信息，也开始承载 `ServerCall` 请求的字段级校验约束。

当前 `ServerCallRequestValidation` 会优先读取字段上的 `Meta`，再执行少量保留的自定义 `TRequestValidator<>`。

已经接入的常见规则包括：

- `NonZero`
- `NonEmpty`
- `Required`
- `Min`
- `Max`
- `ErrorCode`
- `ErrorContext`

更完整的写法约定见 `Docs/RequestValidation.md`。

## RPC 运行时分层

### 1. 传输层

- `MServerConnection`
- `INetConnection`
- `RpcTransport`

负责字节流连接、发包收包，不理解具体业务。

### 2. 调度与元数据层

- `RpcDispatch`
- `RpcManifest`
- `RpcServerCall`
- `RpcClientCall`

负责根据函数 ID、函数签名和目标对象完成调用派发。

### 3. 运行时上下文层

- `IRpcTransportResolver`
- `IServerRuntimeContext`
- `MServerRuntimeContext`

作用是把“这个服务器当前可访问哪些下游服连接”统一收口，避免业务层手工传递大量连接句柄。

### 4. 负载编解码层

- `BuildPayload`
- `ParsePayload`

它们直接依赖反射元数据，把 `MSTRUCT` 消息对象与二进制负载互转。

## 客户端调用

客户端统一连到 `GatewayServer`，但业务 `ClientCall` 不再要求挂在 Gateway 本体上。

当前主模式是：

1. Gateway 根据 `MClientManifest` 查函数 ID、目标服和绑定器
2. 如果目标是网关本地能力，则本地派发
3. 如果目标是业务服，则转发 `FForwardedClientCallRequest`
4. 业务服上的 `ServiceEndpoint` 真正执行 `ClientCall`

当前 World 上的主要客户端入口位于 `MWorldClient`，已经覆盖：

- `Client_Login`
- `Client_FindPlayer`
- `Client_Move`
- `Client_QueryProfile`
- `Client_QueryPawn`
- `Client_QueryInventory`
- `Client_QueryProgression`
- `Client_Logout`
- `Client_SwitchScene`
- `Client_ChangeGold`
- `Client_EquipItem`
- `Client_GrantExperience`
- `Client_ModifyHealth`
- `Client_CastSkill`

这些 `ClientCall` 入口现在不再额外维护一套 `FClient*Request` 业务请求类型。

当前约定是：

- Player 业务请求直接使用 `FPlayer*Request`
- World 编排请求直接使用 `FWorld*Request`
- `FClient*` 主要保留给客户端 response / notify，以及少数纯客户端语义请求（如登录）

## 普通 Player RPC 链路

当前普通 Player 业务调用已经不是“入口层手写胶水堆逻辑”，而是走一条相对标准化的绑定链：

1. `MWorldClient::Client_*`
2. `MWorldClientCommon::StartAsyncClientResponse / FRequest::Dispatch`
3. `MPlayerService::Player*`
4. `DispatchRuntimeCommand / DispatchRuntimeCommandMany`
5. `MPlayer` 子对象或 `MPlayerService::DoXxx(...)`

这条链路适合承载：

- 查询型 Player RPC
- 单玩家局部写操作
- 不需要跨多个服务协作的业务

不适合承载：

- 登录
- 进世界
- 切场景
- 登出
- 任何需要 Login / Scene / Router / Mgo 多步协作的流程

这些仍应保留在 World 的显式编排入口里，而不是回退成普通 Player route。

## Client API 稳定 ID

`ClientCall` 的稳定 ID 使用固定作用域 `MClientApi`，不依赖 owner class 名。

- 默认稳定 ID 来源是函数名
- 如果要在重命名函数或迁移 owner 时保持旧 ID，可显式指定 `Api=...`
- 生成代码和脚本统一使用稳定 API 名计算 ID

这意味着把 `Client_Login` 从某个旧 owner 挪到 `MWorldClient`，客户端函数 ID 仍然可以不变。

## 服间调用

各服之间通过 `MFUNCTION(ServerCall)` 与对应的 `*Rpc` 包装类完成强类型调用，例如：

- `Login` 会话签发与校验
- `Router` 路由注册与查询
- `Scene` 进出场景和战斗接口
- `Mgo` 保存与加载玩家快照

当前比较典型的跨服 workflow 包括：

- `PlayerEnterWorld`
- `PlayerSwitchScene`
- `PlayerLogout`
- `MPlayerService::CastSkill`

## 当前调用边界收敛状态

这一轮仓库已经把 World 相关调用边界进一步收口到三层：

### 1. `WorldClient*` 只做 client-call 适配

- `MWorldClient`
- `MWorldClientCommon`

当前职责：

- 捕获 `ClientCall` 上下文
- 把客户端请求投影到 `PlayerService` 或少数显式编排入口
- 把 `TResult<..., FAppError>` 投影回客户端响应

当前已经收口：

- `StartAsyncClientResponse(...)` 统一了承接 `CaptureCurrentClientCallContext + StartDeferredFiber + failure mapping`
- 普通 Player 请求和 `Client_CastSkill` 已复用统一投影路径

### 2. `PlayerService` 负责 Runtime Dispatch 契约

- `PrepareRuntimeDispatch`
- `DispatchRuntimeCommand`
- `DispatchRuntimeCommandMany`

当前职责：

- 请求校验
- 依赖可用性检查
- 进入 `PlayerCommandRuntime`
- 把真正业务步骤下沉到 `DoXxx(...)`
- `FiberAwait` 现在会先检查 backend 能力；不支持 suspend/resume 的平台会显式 fail-fast，而不是把 null backend 异常直接漏给业务层

### 3. `ObjectCall*` 负责对象级本地/远端调用

- `MObjectCall::CallRaw / Call / CallLocalRaw`
- `MObjectCallRouter`

当前职责：

- 目标 server 判定
- 本地对象 resolve
- 本地 `ServerCall` 响应桥接
- 远端对象代理调用

当前已经收口：

- `ResolveTargetServerType / ResolveLocalTargetObject / ParsePayloadResult`
- `DispatchLocalRaw(...)` 内的本地 response bridge 已拆成局部 helper
- `MObjectCallRouter` 已对齐到同一套本地 target resolve 语义

这三层的目标不是继续发明新 owner，而是明确：

- `WorldClient` 不长成业务层
- `PlayerService` 不长成 transport glue
- `ObjectCall` 不长回“大函数包所有细节”

## 客户端下行

当前项目有两类客户端下行入口：

### 1. `MClientDownlink`

用于定义客户端可接收的反射函数，例如：

- `Client_OnObjectCreate / Update / Destroy`
- `Client_ScenePlayerEnter / Update / Leave`

复制驱动和场景同步都会依赖这类 downlink 函数 ID。

### 2. `PushClientDownlink`

业务服不直接写客户端 socket，而是：

1. 构造 payload
2. 带着 `GatewayConnectionId` 调用 Gateway 的 `PushClientDownlink`
3. Gateway 再推给客户端连接

这条链路当前已经用于场景玩家进入、更新、离开等下行消息。

## 并发模型

当前并发运行时建议按三层理解。

### `MPromise`

生产异步结果的完成端。

使用场景：

- 你手里控制“何时完成”
- 你需要从回调、线程池任务、网络响应中手动 `SetValue` / `SetException`

### `MFuture`

消费异步结果的句柄。

当前支持：

- `Wait()`
- `Await()`
- `Get()`
- `Then(...)`

语义建议：

- `Await()` 用在你明确要同步等结果的地方
- `Then(...)` 用在还想继续链式异步处理时

### `MCoroutine`

封装“多步异步流程”的工作流对象，本质上内部仍然依赖 `Promise/Future` 传播最终结果。

适用场景：

- 一个业务流程包含多个阶段
- 你不想在 `Server` 或 `Endpoint` 里叠很多 Lambda
- 你希望把流程状态显式保存为对象

## `MAsync` 的定位

`MAsync` 更偏调度工具层，而不是业务工作流层。

当前职责：

- `Run`
  把同步任务投递到线程池并返回 `MFuture`
- `Post`
  把任务投递到某个 `ITaskRunner`
- `Yield`
  将后续步骤安排到下一 tick
- `StartCoroutine`
  托管 `MCoroutine` 生命周期并返回其 `MFuture`

## 当前推荐写法

### 简单异步结果

当事情本质上只有“一次完成”时：

- 用 `MPromise + MFuture`

### 多步业务流程

当逻辑跨越多个 RPC、包含错误分支与状态推进时：

- 用显式 `Workflow` 或 `MCoroutine`
- 把流程状态留在对象里，而不是叠很多 `Then(lambda)`

### 普通 Player 业务

当请求天然带 `PlayerId`，并且最终会落到某个 Player 子对象上执行时：

- 优先走当前的 Player RPC 标准链路

### 不推荐

- 在 `Server.cpp` 里连续堆多层 `Then(lambda)` 嵌套
- 为了等待结果强行引入 `co_await`
- 将底层连接对象直接穿透到业务层
- 把登录、切场这类 workflow 强行塞进普通 Player route list

## 当前仍待继续收敛的点

- `Then(...)` 只有基础形态，高阶链式组合还比较原始
- 一些多步业务流程仍有 Lambda 残留
- 主线程、事件循环、线程池三者的推荐边界还可以再细化
- 客户端下行、复制驱动和业务场景同步之间的职责说明还值得继续压实
