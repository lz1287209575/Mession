# UE 客户端骨架设计

本文档给 UE 侧一套面向当前主线协议的客户端骨架设计。

目标不是做一个“只够登录一次”的样板，而是先搭一套可以承接当前 16 个客户端入口、3 条场景下行、后续继续扩展的基础结构。

## 设计原则

当前 UE 骨架建议遵循以下原则：

- 传输层和业务层分开
- 请求/响应和下行推送分开
- 反射编解码和 API 封装分开
- 先支持当前仓库真实能力，不提前抽象完整复制框架
- 保留稳定 `FunctionId` 算法，不依赖手写 magic number

## 推荐目录

```text
Source/<YourGame>/Mession/
    MessionTypes.h
    MessionByteCodec.h
    MessionByteCodec.cpp
    MessionReflectCodec.h
    MessionReflectCodec.cpp
    MessionProtocolCodec.h
    MessionProtocolCodec.cpp
    MessionTcpClient.h
    MessionTcpClient.cpp
    MessionGatewaySubsystem.h
    MessionGatewaySubsystem.cpp
    MessionPlayerSyncSubsystem.h
    MessionPlayerSyncSubsystem.cpp
```

如果项目不想拆两个 Subsystem，也可以合并到一个 `UMessionClientSubsystem`，但至少要保留“连接/协议/同步”三层边界。

## 推荐模块划分

### 1. `FMessionTcpClient`

职责：

- 建立和关闭 TCP 连接
- 处理 `uint32` 长度前缀封包
- 接收缓冲累积与拆包
- 对上层输出完整 `PacketBody`

它不负责：

- 反射序列化
- `FunctionId`
- 业务状态
- 重试和重登策略

### 2. `FMessionProtocolCodec`

职责：

- 编解码客户端请求/响应包
- 编解码客户端下行包
- 管理 `CallId`
- 计算稳定 `FunctionId`

它至少要支持两种包：

- ClientCall：
  - `uint8 MsgType`
  - `uint16 FunctionId`
  - `uint64 CallId`
  - `uint32 PayloadSize`
  - `Payload`
- ClientDownlink：
  - `uint8 MsgType`
  - `uint16 FunctionId`
  - `uint32 PayloadSize`
  - `Payload`

### 3. `FMessionReflectCodec`

职责：

- 基础类型编码/解码
- `MString <-> FString` 的 UTF-8 转换
- 当前几个客户端结构的顺序编解码

第一版至少支持：

- `bool`
- `uint16`
- `uint32`
- `uint64`
- `int32`
- `float`
- `FString`

### 4. `UMessionGatewaySubsystem`

职责：

- 维护连接状态
- 发起心跳
- 发起 `Client_Login / Query / Write` 请求
- 跟踪 Pending Call
- 保存本地玩家会话信息

建议缓存：

- `ConnectionState`
- `NextCallId`
- `LoggedInPlayerId`
- `SessionKey`
- `GatewayConnectionId`
- `LastHeartbeatSequence`

### 5. `UMessionPlayerSyncSubsystem`

职责：

- 处理 `Client_ScenePlayerEnter`
- 处理 `Client_ScenePlayerUpdate`
- 处理 `Client_ScenePlayerLeave`
- 维护远端玩家缓存
- 驱动远端玩家表现层

建议缓存：

- `TMap<uint64, FRemoteScenePlayerState>`

## 类型建议

建议把当前 UE 侧真正要用到的协议结构集中到一个头文件，不要散落在各个业务类里。

### `MessionTypes.h`

推荐至少包含：

- `FMessionLoginRequest`
- `FMessionLoginResponse`
- `FMessionHeartbeatRequest`
- `FMessionHeartbeatResponse`
- `FMessionFindPlayerRequest`
- `FMessionFindPlayerResponse`
- `FMessionQueryProfileResponse`
- `FMessionQueryPawnResponse`
- `FMessionQueryInventoryResponse`
- `FMessionQueryProgressionResponse`
- `FMessionMoveRequest`
- `FMessionMoveResponse`
- `FMessionSwitchSceneRequest`
- `FMessionSwitchSceneResponse`
- `FMessionChangeGoldRequest`
- `FMessionChangeGoldResponse`
- `FMessionEquipItemRequest`
- `FMessionEquipItemResponse`
- `FMessionGrantExperienceRequest`
- `FMessionGrantExperienceResponse`
- `FMessionModifyHealthRequest`
- `FMessionModifyHealthResponse`
- `FMessionCastSkillRequest`
- `FMessionCastSkillResponse`
- `FMessionScenePlayerState`
- `FMessionScenePlayerLeave`

第一版可以先只实现已接入到 UI/测试流程的那部分，但类型层建议一开始就把命名规范定稳。

## 协议层建议

### 稳定 ID

`FunctionId` 不要长期硬编码。

建议保留：

```cpp
uint16 ComputeStableClientFunctionId(const FString& Name);
uint16 ComputeStableDownlinkFunctionId(const FString& Name);
```

对应规则：

- Client API: `ComputeStableReflectId("MClientApi", Name)`
- Downlink: `ComputeStableReflectId("MClientDownlink", Name)`

### 包体判定

收到一个完整 `PacketBody` 后，建议按以下顺序判断：

1. `MsgType` 是否为 `13`
2. 如果长度足够解析 `FunctionId + CallId + PayloadSize`，优先尝试按请求响应包解析
3. 如果不满足，再尝试按 downlink 包解析
4. 解析失败则记协议错误并丢弃

更稳妥的实现方式是：

- 先由 Pending Call 表判断该包是否可能属于一个响应
- 再决定是否按 downlink 解释

这样可以减少把异常响应误判成下行的风险。

## 请求分发建议

不要一开始做一套通用模板元编程 RPC 框架。

当前最实用的结构是：

- `SendHeartbeat`
- `Login`
- `FindPlayer`
- `QueryProfile`
- `QueryPawn`
- `QueryInventory`
- `QueryProgression`
- `Move`
- `SwitchScene`
- `ChangeGold`
- `EquipItem`
- `GrantExperience`
- `ModifyHealth`
- `CastSkill`

每个公开方法：

1. 构造请求结构
2. 编码 payload
3. 分配 `CallId`
4. 发送包
5. 在 Pending Map 中登记回调

这样最贴近当前代码和联调需求。

## Pending Call 设计

建议使用：

```text
TMap<uint64, FPendingClientCall>
```

每个 `FPendingClientCall` 至少包含：

- `FunctionId`
- `Deadline`
- 成功回调
- 失败回调

当前单连接模型下，只按 `CallId` 建表已经够用，不需要一上来就做复合 Key。

## 心跳与会话状态

`Client_Heartbeat` 当前在 Gateway 本地处理，不转发到 World。

这意味着：

- 心跳可以在尚未登录时工作
- 心跳更像连接保活和连通性确认
- 登录态是否有效，仍应以 `Client_Login` 和后续查询结果为准

建议在 `UMessionGatewaySubsystem` 中维护：

- 连接成功后定时心跳
- 连续多次心跳失败触发断开
- 断开后清空 `SessionKey` 与已登录玩家状态

## 场景同步设计

当前不建议 UE 侧直接以“完整对象复制系统”去吃服务端下行。

更现实的第一版是：

1. `Enter` 时创建远端玩家
2. `Update` 时更新远端玩家位置
3. `Leave` 时销毁或隐藏远端玩家

`FRemoteScenePlayerState` 推荐至少包含：

- `PlayerId`
- `SceneId`
- `FVector Position`
- `double LastUpdateTime`
- 关联的远端 Actor 指针

## 推荐实现顺序

### 第一轮

先把基础连通性做稳：

1. `FMessionTcpClient`
2. `FMessionProtocolCodec`
3. `FMessionReflectCodec`
4. `Client_Heartbeat`
5. `Client_Login`
6. `Client_FindPlayer`

### 第二轮

补全查询：

1. `Client_QueryProfile`
2. `Client_QueryPawn`
3. `Client_QueryInventory`
4. `Client_QueryProgression`

### 第三轮

补全写操作：

1. `Client_Move`
2. `Client_SwitchScene`
3. `Client_ChangeGold`
4. `Client_EquipItem`
5. `Client_GrantExperience`
6. `Client_ModifyHealth`

### 第四轮

补全玩法和场景同步：

1. `Client_CastSkill`
2. `Client_ScenePlayerEnter`
3. `Client_ScenePlayerUpdate`
4. `Client_ScenePlayerLeave`

## 不推荐的设计

- 先做完整模板化 RPC 框架
- 先做对象复制总线
- 先做复杂自动重连状态机
- 在 Socket 回调里直接写业务逻辑
- 把下行消息和请求响应共用同一个不区分来源的处理器

这些都会让第一轮 UE 接入复杂化，但不会更贴近当前仓库实际。

## 验收标准

这个骨架至少应支持：

1. 同一连接上并发追踪多个 `CallId`
2. 正确解析请求响应包和下行推送包
3. 登录后能够继续查询资料和 Pawn 状态
4. 写操作后能看到查询结果更新
5. 双客户端时能看到场景玩家进入、更新、离开
6. 所有失败路径都能落日志，而不是静默吞掉

## 可直接给 UE Agent 的设计说明

```text
请在 UE 工程里搭一个面向当前 Mession 主线协议的客户端骨架，不要只做登录样板。

代码结构建议：
- FMessionTcpClient：TCP 与长度前缀拆包
- FMessionProtocolCodec：FunctionCall 请求响应与 downlink 编解码，负责 FunctionId 和 CallId
- FMessionReflectCodec：反射顺序序列化
- UMessionGatewaySubsystem：连接、心跳、登录、查询、写操作
- UMessionPlayerSyncSubsystem：ScenePlayerEnter/Update/Leave 和远端玩家缓存

请优先支持：
- Heartbeat
- Login
- FindPlayer
- QueryProfile
- QueryPawn
- QueryInventory
- QueryProgression
- Move
- SwitchScene

然后再补：
- ChangeGold
- EquipItem
- GrantExperience
- ModifyHealth
- CastSkill
- ScenePlayerEnter/Update/Leave

不要先做完整对象复制框架，也不要先做复杂模板化 RPC 抽象。
```
