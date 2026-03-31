# UE Player 同步接入 Agent 文档

本文档面向 UE 侧 Agent，描述当前仓库已经落地的“场景内玩家同步”接入方式。

适用日期：`2026-03-31`

## 1. 目标边界

当前这套同步不是完整对象复制框架对接，也不是全量属性同步。

当前已经稳定落地、适合 UE 先接的只有 3 条场景玩家下行：

- `Client_ScenePlayerEnter`
- `Client_ScenePlayerUpdate`
- `Client_ScenePlayerLeave`

它们的用途是：

- 有其他玩家进入和你同一个场景时，通知你生成远端玩家表现
- 有其他玩家在同场景内移动时，通知你刷新远端玩家位置
- 有其他玩家离开当前场景或登出时，通知你移除远端玩家表现

## 2. 当前真实链路

当前服务端链路是：

1. `WorldServer` 在玩家登录、切场、移动、登出时触发广播
2. `WorldServer` 把下行消息打包成 `FClientDownlinkPushRequest`
3. `GatewayServer::PushClientDownlink` 把消息推回客户端连接
4. 客户端收到的是 `MT_FunctionCall`
5. 但这是“客户端下行函数包”，不是普通上行请求包

也就是说，UE 侧要支持两种 `MT_FunctionCall`：

1. 上行请求：有 `CallId`
2. 下行推送：没有 `CallId`

## 3. 下行包格式

### 3.1 TCP 外层

仍然是 length-prefixed：

```text
uint32 PacketLength
PacketBody bytes...
```

### 3.2 Player 同步下行的 `PacketBody`

当前下行使用 `BuildClientFunctionPacket(...)`，格式是：

```text
uint8  MsgType
uint16 FunctionId
uint32 PayloadSize
Payload bytes...
```

固定值：

- `MsgType = 13`

注意：

- 这里没有 `CallId`
- 所以 UE 不能按普通请求响应包去解这个下行

## 4. FunctionId 计算规则

这 3 条下行函数不是按 `MClientApi` 计算，而是按 `MClientDownlink` 作用域计算：

```text
StableId = ComputeStableReflectId("MClientDownlink", FunctionName)
```

当前可直接使用的 FunctionId：

- `Client_ScenePlayerEnter = 2660`
- `Client_ScenePlayerUpdate = 43756`
- `Client_ScenePlayerLeave = 1143`

如果 UE 侧已经实现了稳定 ID 算法，也可以按 `MClientDownlink` 作用域动态计算。

## 5. 三条下行消息的 payload

### 5.1 `Client_ScenePlayerEnter`

函数名：

- `Client_ScenePlayerEnter`

payload 类型：

- `SPlayerSceneStateMessage`

结构：

```cpp
struct SPlayerSceneStateMessage
{
    uint64 PlayerId = 0;
    uint16 SceneId = 0;
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
};
```

字段顺序：

```text
uint64 PlayerId
uint16 SceneId
float  X
float  Y
float  Z
```

UE 侧语义：

- 如果本地还没有这个 `PlayerId` 的远端表现，则创建
- 把它放到 `SceneId` 对应的场景上下文中
- 初始位置设为 `X/Y/Z`

### 5.2 `Client_ScenePlayerUpdate`

函数名：

- `Client_ScenePlayerUpdate`

payload 类型仍然是：

- `SPlayerSceneStateMessage`

字段和 `Enter` 完全相同：

```text
uint64 PlayerId
uint16 SceneId
float  X
float  Y
float  Z
```

UE 侧语义：

- 找到已有远端玩家
- 更新其场景与位置
- 如果本地还没建这个玩家，可记录 warning，或者退化成一次“补建 + 更新”

### 5.3 `Client_ScenePlayerLeave`

函数名：

- `Client_ScenePlayerLeave`

payload 类型：

- `SPlayerSceneLeaveMessage`

结构：

```cpp
struct SPlayerSceneLeaveMessage
{
    uint64 PlayerId = 0;
    uint16 SceneId = 0;
};
```

字段顺序：

```text
uint64 PlayerId
uint16 SceneId
```

UE 侧语义：

- 从当前远端玩家缓存中移除该 `PlayerId`
- 销毁对应表现实体或标记离场

## 6. 当前服务端触发时机

当前触发逻辑如下：

### 6.1 `Client_ScenePlayerEnter`

由 `WorldServer::QueueScenePlayerEnterBroadcast(...)` 触发，典型时机：

- 玩家登录成功进入世界后
- 玩家切场成功后

广播规则：

- 新进入的玩家会收到“同场景其他玩家”的 enter
- 同场景其他玩家也会收到“这个新玩家”的 enter

所以 UE 侧不要假设 enter 只会发给别人，不会发给新登录者自己。

### 6.2 `Client_ScenePlayerUpdate`

由 `WorldServer::QueueScenePlayerUpdateBroadcast(...)` 触发，当前时机：

- `Client_Move` 成功后
- `Client_ModifyHealth` 成功后

但当前 `SPlayerSceneStateMessage` 里并没有 `Health` 字段，所以对 UE 来说，当前主要可用的是位置刷新。

### 6.3 `Client_ScenePlayerLeave`

由 `WorldServer::QueueScenePlayerLeaveBroadcast(...)` 触发，当前时机：

- 玩家登出后
- 玩家切场离开旧场景后

## 7. UE 侧最低实现建议

建议 UE Agent 最少做这几个模块：

### 7.1 Downlink 解包层

识别：

- `MsgType = 13`
- 如果 body 是 `uint16 FunctionId + uint32 PayloadSize + Payload`
- 且没有 `CallId`

则按“客户端下行推送”处理。

### 7.2 PlayerSync 管理层

建议维护：

```text
TMap<uint64, FRemoteScenePlayerState>
```

其中至少保存：

- `PlayerId`
- `SceneId`
- `X`
- `Y`
- `Z`

### 7.3 表现层

推荐先做简单版本：

- `Enter` -> Spawn/Show Remote Player
- `Update` -> SetActorLocation 或插值移动
- `Leave` -> Destroy/Hide Remote Player

## 8. 最小验收标准

UE Agent 完成后，至少满足：

1. 登录玩家 A
2. 登录玩家 B，并切到和 A 相同场景
3. A 能收到 `Client_ScenePlayerEnter(B)`
4. A 移动后，B 能收到 `Client_ScenePlayerUpdate(A)`
5. A 登出后，B 能收到 `Client_ScenePlayerLeave(A)`

## 9. 当前验证事实

当前仓库的 `validate.py` 已经覆盖这 3 条同步：

- `Client_ScenePlayerEnter`
- `Client_ScenePlayerUpdate`
- `Client_ScenePlayerLeave`

所以 UE 侧接入时，建议直接对齐这三条消息做最小闭环。

## 10. 可直接发给 UE Agent 的 Prompt

```text
请在 UE 工程中接入 Mession 当前已经落地的场景玩家同步下行，只做 3 条消息：
Client_ScenePlayerEnter、Client_ScenePlayerUpdate、Client_ScenePlayerLeave。

注意这些消息虽然也是 MT_FunctionCall(MsgType=13)，但它们是客户端下行推送，不带 CallId。
它们的包体格式是：
uint16 FunctionId + uint32 PayloadSize + Payload。

FunctionId 固定为：
- Client_ScenePlayerEnter = 2660
- Client_ScenePlayerUpdate = 43756
- Client_ScenePlayerLeave = 1143

Enter 和 Update 的 payload 都是：
uint64 PlayerId + uint16 SceneId + float X + float Y + float Z

Leave 的 payload 是：
uint64 PlayerId + uint16 SceneId

请在 UE 中维护一个 RemotePlayers 映射：
- 收到 Enter -> 创建或显示远端玩家
- 收到 Update -> 更新远端玩家位置
- 收到 Leave -> 移除远端玩家

第一版不要接完整复制系统，不要做通用对象同步，只做这 3 条场景玩家消息。
完成后请给出文件列表、类职责、触发方式，以及一段双玩家联调日志。
```
