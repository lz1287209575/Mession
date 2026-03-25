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

## 客户端调用与服间调用

### 客户端调用

客户端统一连到 `GatewayServer`，但业务 `ClientCall` 不再要求挂在 Gateway 本体上。

当前模式是：

- Gateway 根据 `MClientManifest` 查函数 ID、目标服、绑定器
- 若目标是本地网关能力，则本地派发
- 若目标是业务服，则转发 `FForwardedClientCallRequest`
- 业务服上的 `ServiceEndpoint` 真正执行 `ClientCall`

例如：

- `MGatewayServer::Client_Echo`
- `MWorldClientServiceEndpoint::Client_Login`
- `MWorldClientServiceEndpoint::Client_FindPlayer`
- `MWorldClientServiceEndpoint::Client_SwitchScene`
- `MWorldClientServiceEndpoint::Client_Logout`

当前最小闭环统一走 `MT_FunctionCall`。

### Client API 稳定 ID

`ClientCall` 的稳定 ID 现在使用固定作用域 `MClientApi`，不再依赖 owner class 名。

- 默认稳定 ID 来源是函数名
- 如果要在重命名函数或迁移 owner 时保持旧 ID，可显式指定 `Api=...`
- 生成代码和脚本统一使用 `MGET_STABLE_CLIENT_FUNCTION_ID("Client_Login")`

这意味着我们可以把 `Client_Login` 从 `MWorldServer` 迁到 `MWorldClientServiceEndpoint`，而客户端函数 ID 不变。

### 服间调用

各服之间通过 `MFUNCTION(ServerCall)` 与对应的 `*Rpc` 包装类完成强类型调用，例如：

- `MWorldServer::PlayerEnterWorld`
- `MLoginServer::IssueSession`
- `MSceneServer::EnterScene`
- `MMgoServer::SavePlayer`

## 并发模型

当前并发运行时分为三层，建议明确区分用途。

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
- `Then(...)` 用在你还想继续链式异步处理时

这里的 `Await()` 是普通成员函数，不依赖 C++20 `co_await`，因此不会把项目锁死在协程语法糖上。

### `MCoroutine`

封装“多步异步流程”的工作流对象，本质上内部仍然依赖 `Promise/Future` 传播最终结果。

适用场景：

- 一个业务流程包含多个阶段
- 你不想在 `Server` 或 `Endpoint` 里叠很多 Lambda
- 你希望把流程状态显式保存为对象

## `MAsync` 的定位

`MAsync` 更偏“调度工具层”，不是业务工作流层。

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

- 用 `MCoroutine` 或独立 `Workflow` 类
- Gateway 只做转发时，不要把业务链塞回 Gateway
- World/Scene/Login 的业务 `ClientCall` 优先落在各自 `ServiceEndpoint`

## 新增一个 Client RPC 的推荐步骤

1. 在目标业务服的 `Services/*Endpoint*` 上声明 `MFUNCTION(ClientCall, Target=目标服)`
2. 请求/响应结构使用 `MSTRUCT + MPROPERTY`
3. 如需保持旧客户端 ID，显式补 `Api=稳定名`
4. 编译触发 `MHeaderTool` 更新 `MClientManifest.generated.h`
5. 运行 `python3 Scripts/validate.py --build-dir Build --no-build`

### 不推荐

- 在 `Server.cpp` 里连续堆多层 `Then(lambda)` 嵌套
- 为了等待结果强行引入 `co_await`
- 将底层连接对象直接穿透到所有业务层

## 当前仍待收敛的点

- `Then(...)` 只有基础形态，高阶链式组合还比较原始
- 一些异步流程仍有 Lambda 残留，后续应继续向显式 Workflow 收敛
- 任务执行上下文与主线程归属规则还可以再细化
