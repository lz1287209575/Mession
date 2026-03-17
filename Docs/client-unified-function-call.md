# Client Unified Function Call Design

## Goal

客户端接入的长期方向调整为：

- 业务层不再显式维护客户端协议号
- Client / Gateway / OtherSvr 统一按函数声明理解网络入口
- `FunctionName` 作为逻辑唯一标识
- `FunctionID` 作为由统一规则自动派生的传输标识

这份文档定义“客户端统一函数调用”方案的目标、边界、wire format、Gateway 行为、兼容策略和迁移顺序。

## Why We Are Changing Direction

当前 `Client <-> Gateway` 仍然是：

- `MessageType + PayloadStruct`

这条路的短期优点是简单、稳定，但长期有三个明显问题：

1. 业务同学要理解两套心智
   一套是客户端消息号；另一套是服务内部函数/RPC。
2. 客户端和服务端声明源分裂
   新增能力时，很容易一边加消息枚举，一边再补函数绑定。
3. Gateway 容易继续承担协议翻译胶水
   即使有生成链路，客户端入口依然还保留“外部协议”和“内部函数”两层映射。

我们想要的终态是：

- 写业务时只写函数和 payload
- 路由、鉴权、转发、编码由工具链和运行时处理
- 不懂服务器实现的同学，也能像写普通客户端调用一样接网络能力

## Non-Goals

这次设计不追求下面几件事：

- 不让客户端直接裸暴露内部所有 Server RPC
- 不抹掉 Gateway 的安全边界、鉴权、限流、路由职责
- 不要求一步删光现有 `MessageType` 兼容路径
- 不要求 UE 手工维护 `FunctionID`

## Core Model

统一函数调用模型下，业务侧维护的唯一事实来源是函数声明：

```cpp
MFUNCTION(Client, Route=RouterResolved, Target=World, Auth=Required, Wrap=PlayerClientSync)
void Client_Chat(uint64 ClientConnectionId, const SClientChatPayload& ChatPayload);
```

对业务层来说，入口语义就是：

- `ClassName`
- `FunctionName`
- Payload
- Route / Auth / Wrap / Target

传输层细节则自动派生：

- `FunctionID`
- Payload 编码方式
- 是否需要 GatewayLocal / Login / RouterResolved
- 是否需要包装成 `PlayerClientSync` 或其他统一策略

## Function Identity Rule

统一函数调用使用的稳定标识规则见：

- [function-id-rules.md](/workspaces/Mession/Docs/function-id-rules.md)

当前统一约束：

- 逻辑唯一标识：`ClassName + FunctionName`
- 传输唯一标识：`ComputeStableReflectId(ClassName, FunctionName)`
- UE / Agent / Server 必须复用完全相同的算法

因此，客户端不需要手工指定 `FunctionID`，只需要提供：

- `ClassName`
- `FunctionName`
- Payload

## Invocation Flow

长期目标下，客户端发送路径如下：

1. 业务层发起 `CallGenerated(ClassName, FunctionName, Payload)`
2. 运行时查找本地生成 manifest 或静态 helper
3. 自动计算稳定 `FunctionID`
4. 自动编码 payload
5. 自动构造客户端函数调用包
6. Gateway 解包后按 `FunctionID` 或对应 manifest 找到目标函数
7. Gateway 按函数声明上的 Route / Auth / Wrap / Target 执行本地消费或转发

对业务层来说，不再需要显式参与：

- `MessageType`
- 客户端 `switch`
- 手工 `FunctionName -> MessageType` 映射

## Chosen Migration Format

迁移期采用固定统一调用消息头：

```text
Length(4 bytes, little-endian)
MsgType(1 byte = MT_FunctionCall)
FunctionID(2 bytes, little-endian)
PayloadSize(4 bytes, little-endian)
Payload(N bytes)
```

其中：

- `Length` 表示后续 `MsgType + FunctionID + PayloadSize + Payload` 的总长度
- `MsgType` 只作为迁移期兼容外壳存在，不再按业务能力扩张
- `FunctionID` 由 `ClassName + FunctionName` 自动派生
- `PayloadSize` 允许 Gateway 做统一边界校验，并与现有 server-rpc 包结构保持一致

当前建议新增一个固定客户端消息类型，例如：

- `MT_FunctionCall`

它的职责不是表达业务含义，而只是说明“这是一包统一函数调用”。

## Why This Format

选择这套格式，而不是直接上“纯 `FunctionID` 包”，有三个原因：

1. 更容易与现有客户端收发包路径共存
2. 更容易在 Gateway 中和旧 `MessageType` 路径做并行分发
3. 更容易做抓包、日志和 debug JSON 观测

长期如果统一函数调用已经完全稳定，再评估是否去掉外层 `MsgType`。

## Packet Examples

### Example 1. Client_Handshake

```text
Length
MT_FunctionCall
FunctionID(ComputeStableReflectId("MGatewayServer", "Client_Handshake"))
PayloadSize(8)
PlayerId(8)
```

### Example 2. Client_Login

```text
Length
MT_FunctionCall
FunctionID(ComputeStableReflectId("MGatewayServer", "Client_Login"))
PayloadSize(8)
PlayerId(8)
```

### Example 3. Client_Chat

```text
Length
MT_FunctionCall
FunctionID(ComputeStableReflectId("MGatewayServer", "Client_Chat"))
PayloadSize(N)
ChatPayload(...)
```

## Versioning Rule

当前阶段不单独引入额外协议版本字段。

版本控制先通过以下方式完成：

- `FunctionID` 稳定规则固定
- Gateway 白名单控制哪些函数当前可调用
- 兼容期保留旧 `MessageType` 路径
- 如需跨版本能力协商，优先通过 handshake / debug manifest / 明确兼容窗口解决

也就是说，这一轮先解决“统一调用模型”，不同时引入复杂版本协商层。

## Gateway Decode Flow

Gateway 对 `MT_FunctionCall` 的处理建议固定为：

1. 读取 `FunctionID`
2. 读取 `PayloadSize`
3. 校验 `PayloadSize` 与包体边界
4. 在生成的 client-callable manifest 中查找 `FunctionID`
5. 若找不到，记录 unknown-function 观测并拒绝
6. 若找到但函数未标记为客户端可见，拒绝
7. 按 binder 解码 payload
8. 按函数声明上的 `Auth / Route / Wrap / Target` 执行

其中第 4 步以后，运行时不再按业务消息号分支。

## Gateway Runtime Data Needed

为支持上面的 decode flow，Gateway 至少要有以下生成数据：

- `FunctionID -> OwnerType`
- `FunctionID -> FunctionName`
- `FunctionID -> BindParams`
- `FunctionID -> RouteName`
- `FunctionID -> AuthMode`
- `FunctionID -> WrapMode`
- `FunctionID -> TargetServerType`
- `FunctionID -> client-callable flag`

当前 `MClientManifest` 更接近：

- `MessageType -> Function`

因此下一阶段需要把它升级成：

- `FunctionID -> FunctionEntry`

## Error Handling

统一函数调用路径必须给出明确错误语义，而不是只打一条模糊日志。

建议至少区分以下错误：

- `UnknownFunctionId`
- `FunctionNotClientCallable`
- `AuthRequired`
- `PayloadDecodeFailed`
- `UnsupportedRoutePolicy`
- `TargetUnavailable`
- `RouteResolvePending`

当前阶段不要求这些错误全部都形成独立下行包；
但至少要满足下面两个条件：

1. Gateway debug JSON 中可观测
2. 日志中能明确区分错误类别

其中：

- `RouteResolvePending` 不一定视为失败，它也可能是正常的 pending/replay 状态
- `TargetUnavailable` 与 `RouteResolvePending` 需要区分，避免脚本误判

## Debug / Observability Requirements

为便于联调和脚本验证，建议补以下观测字段：

- `clientFunctionCallCount`
- `clientFunctionCallRejectedCount`
- `lastClientFunctionId`
- `lastClientFunctionName`
- `lastClientFunctionError`
- `unknownClientFunctionCount`
- `clientFunctionDecodeFailureCount`

这些字段应进入 Gateway debug JSON，而不是只存在日志。

## Gateway Runtime Requirements

如果客户端入口切到统一函数调用，Gateway 至少需要补齐以下能力：

1. `FunctionID -> Client Function Entry` 查找
2. payload bind/decode helper
3. Route / Auth / Wrap / Target 执行
4. 未知 `FunctionID` 的明确拒绝与 debug 观测
5. 客户端可调用函数白名单
6. 与旧 `MessageType` 路径双轨共存的入口分发

其中第 5 点很关键：

- 统一函数调用不等于“客户端可以调用任意内部 RPC”
- 只有声明为客户端可见入口的函数，才能进入这条路径

## MHeaderTool Responsibilities

为支持统一函数调用，`MHeaderTool` 后续应补以下生成产物：

1. Client-callable manifest
2. `FunctionID <-> FunctionName` 映射
3. payload binder / decoder
4. Gateway client invoke dispatch glue
5. UE / Agent 可消费的导出 manifest 或静态 helper
6. `FunctionID` 冲突检查与构建期失败或明确告警

换句话说，生成器不再只是输出：

- `MessageType <-> Function`

而是要升级为输出：

- `Client-callable Function <-> FunctionID <-> binder <-> route policy`

## Validation Requirements

统一函数调用至少要补下面这些验证：

- 关键函数的双端 `FunctionID` 一致性校验
- 已认证函数的正常路由转发
- 未认证函数的明确拒绝
- 未知 `FunctionID` 的明确拒绝
- payload decode 失败的明确拒绝
- route cache 命中 / 失效 / replay
- 兼容旧 `MessageType` 路径与新函数调用路径共存验证

首批建议直接补的验证：

- `Client_Handshake` 通过 `MT_FunctionCall` 本地处理成功
- `Client_Login` 通过 `MT_FunctionCall` 到达 Login 并返回登录结果
- `Client_Chat` 通过 `MT_FunctionCall` 命中 `RouterResolved -> World`
- 未登录状态调用 `Client_Chat` 被明确拒绝
- 构造未知 `FunctionID` 被明确拒绝
- 构造错误 payload size 或错误编码被明确拒绝

## Migration Plan

建议按下面顺序迁移。

### Phase 1. 定规则

- 固化 `FunctionID` 规则
- 明确客户端统一函数调用的包格式
- 明确 Gateway 的白名单与安全边界
- 明确 Gateway debug / 错误观测字段

### Phase 2. 跑通最小垂直切片

建议首批只挑三条：

- `Client_Handshake`
- `Client_Login`
- `Client_Chat`

原因：

- 一条 `GatewayLocal`
- 一条 `Login`
- 一条 `RouterResolved -> World`

这三条刚好覆盖三种主要运行策略。

并且它们的 payload 简单，适合先验证：

- 统一 `FunctionID` 查找
- binder 解码
- 本地处理
- Login 转发
- RouterResolved 转发

### Phase 3. 双轨共存

- 保留旧 `MessageType` 路径
- 新增统一函数调用路径
- 脚本同时验证两条路径行为一致
- 新增能力默认优先从统一函数调用路径接入

### Phase 4. 收口

- 新能力默认只允许走统一函数调用
- 旧的 `MessageType` 路径只保留兼容白名单
- 逐步减少客户端枚举暴露面
- 评估是否可以移除外层 `MsgType` 外壳

## Open Decisions

当前仍需后续拍板的点：

1. `MT_FunctionCall` 是否复用现有枚举空间，还是单独开新值
2. 下行是否同步设计统一函数返回包，还是继续保留现有 `S2C` 消息
3. 构建期对 `FunctionID` 冲突采用 hard fail 还是 warning + 文档约束
4. UE 侧是直接吃导出的 manifest，还是先手写最小 helper + 统一算法

当前建议：

- 先开新 `MT_FunctionCall`
- 下行先不一起大改
- 冲突至少在构建中显式告警，后续尽量升级为 hard fail
- UE 先用统一算法 + 最小 helper 跑通，再考虑 manifest 导出

## Current Decision

当前决策如下：

- 我们**开始设计客户端统一函数调用**
- 客户端业务层长期不再显式关心协议号
- `FunctionID` 由统一规则自动派生，不人工维护
- 短期迁移采用“`MT_FunctionCall + FunctionID + PayloadSize + Payload`”的兼容格式
- 旧 `MessageType` 路径作为迁移期兼容层存在，但不再作为长期主线扩展

## References

- [function-id-rules.md](/workspaces/Mession/Docs/function-id-rules.md)
- [client-protocol-reflection.md](/workspaces/Mession/Docs/client-protocol-reflection.md)
- [ue-gateway-quickstart.md](/workspaces/Mession/Docs/ue-gateway-quickstart.md)
