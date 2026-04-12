# UE 场景玩家同步接入说明

本文档面向 UE 侧 Agent，描述当前仓库已经稳定落地的场景玩家同步方式。

当前最适合 UE 第一轮接入的客户端下行只有 3 条：

- `Client_ScenePlayerEnter`
- `Client_ScenePlayerUpdate`
- `Client_ScenePlayerLeave`

它们已经在 `WorldServer -> GatewayServer -> Client` 链路上工作，并被 `Scripts/validate.py` 覆盖验证。

## 这套同步解决什么问题

它当前解决的是“同场景其他玩家的存在与位置同步”：

- 其他玩家进入你所在场景时，通知你创建远端表现
- 其他玩家在同场景移动时，通知你更新远端位置
- 其他玩家离开场景或登出时，通知你移除远端表现

它还不是完整复制系统，也不是通用对象属性同步。

## 当前真实链路

链路如下：

1. `WorldServer` 在玩家进入、移动、切场、登出等时机决定是否广播
2. `WorldServer` 调用 `QueueClientDownlink(...)`
3. `WorldServer` 构造 `FClientDownlinkPushRequest`
4. `GatewayServer::PushClientDownlink(...)` 把消息推回客户端连接
5. 客户端收到 `MT_FunctionCall`
6. UE 侧按 downlink 方式解包并分发给 PlayerSync 模块

关键位置：

- 下行声明：`Source/Common/Net/ClientDownlink.h`
- 下行传输：`Source/Servers/Gateway/GatewayServer.cpp`
- 广播逻辑：`Source/Servers/World/WorldServer.cpp`

## 下行包格式

TCP 最外层仍是长度前缀：

```text
uint32 PacketLength
PacketBody bytes...
```

场景同步使用的 `PacketBody` 为：

```text
uint8  MsgType
uint16 FunctionId
uint32 PayloadSize
Payload bytes...
```

固定值：

- `MsgType = 13`

注意：

- 下行推送没有 `CallId`
- UE 不能按普通请求响应包去解析这三条消息

## FunctionId 规则

这三条下行不是 `MClientApi` 作用域，而是：

```cpp
ComputeStableReflectId("MClientDownlink", FunctionName)
```

当前已验证 ID：

- `Client_ScenePlayerEnter = 2660`
- `Client_ScenePlayerUpdate = 43756`
- `Client_ScenePlayerLeave = 1143`

推荐 UE 侧把同样算法实现出来，而不是长期手写常量。

## Payload 结构

### `Client_ScenePlayerEnter`

payload 类型：

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

- 若本地没有该 `PlayerId` 的远端表现，则创建
- 设置所属场景
- 设置初始位置

### `Client_ScenePlayerUpdate`

payload 仍是 `SPlayerSceneStateMessage`，字段和 `Enter` 完全一致：

```text
uint64 PlayerId
uint16 SceneId
float  X
float  Y
float  Z
```

UE 侧语义：

- 更新远端玩家位置
- 如果本地还没建这个远端玩家，可选择：
  - 记 warning 后丢弃
  - 或退化为一次补建

### `Client_ScenePlayerLeave`

payload 类型：

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

- 从远端玩家缓存中移除该 `PlayerId`
- 销毁或隐藏对应表现

## 当前触发时机

### `Client_ScenePlayerEnter`

由 `WorldServer::QueueScenePlayerEnterBroadcast(...)` 发送。

当前触发时机：

- 玩家登录成功进入世界后
- 玩家切场成功后

广播语义是双向的：

- 同场景其他玩家会收到“新玩家进入”
- 新进入的玩家也会收到“当前场景已有其他玩家”

UE 侧不要假设只有别人会收到 `Enter`。

### `Client_ScenePlayerUpdate`

由 `WorldServer::QueueScenePlayerUpdateBroadcast(...)` 发送。

当前触发时机：

- `Client_Move` 成功后
- `Client_ModifyHealth` 成功后

但当前 payload 只包含位置与场景，不包含 `Health`。所以对 UE 表现层来说，当前实际主要用途仍是位置刷新。

### `Client_ScenePlayerLeave`

由 `WorldServer::QueueScenePlayerLeaveBroadcast(...)` 发送。

当前触发时机：

- 玩家登出后
- 玩家切场离开旧场景后

## 当前广播规则

当前 World 侧广播时会跳过：

- 自己给自己发 `Update`
- 自己给自己发 `Leave`
- 不在同场景的其他玩家
- 没有有效会话或不能接收 scene downlink 的玩家

因此 UE 侧可以按“只同步同场景其他玩家”来设计远端玩家缓存。

## UE 侧建议模块

### Downlink 解包层

职责：

- 识别 `MsgType = 13`
- 识别无 `CallId` 的 downlink 包
- 按 `FunctionId` 分发到 PlayerSync

### PlayerSync 状态层

建议维护：

```text
TMap<uint64, FRemoteScenePlayerState>
```

至少保存：

- `PlayerId`
- `SceneId`
- `FVector Position`
- 最后更新时间

### 表现层

建议第一版直接做：

- `Enter` -> Spawn 或 Show
- `Update` -> SetActorLocation 或插值更新
- `Leave` -> Destroy 或 Hide

## 当前验证事实

`Scripts/validate.py` 目前已经覆盖：

1. 玩家 A 登录并进入场景
2. 玩家 B 登录并切到相同场景
3. A 收到 `Client_ScenePlayerEnter(B)`
4. A 施法后，B 的状态在查询中体现变化
5. A 再移动后，B 收到 `Client_ScenePlayerUpdate(A)`
6. A 登出后，B 收到 `Client_ScenePlayerLeave(A)`

这说明这三条场景同步消息已经不是占位设计，而是当前主线协议的一部分。

## 当前不建议一起做的内容

第一轮 UE 接入不建议把这些一起打包进来：

- `Client_OnObjectCreate`
- `Client_OnObjectUpdate`
- `Client_OnObjectDestroy`
- 通用对象复制层
- 全量属性插值框架

场景玩家同步先独立接好，复杂对象同步后面再做更稳。

## 建议验收标准

UE 侧完成后，至少应满足：

1. 双客户端登录同一场景
2. 能为远端玩家建立唯一 `PlayerId` 映射
3. 收到 `Enter` 后正确创建表现
4. 收到 `Update` 后正确更新位置
5. 收到 `Leave` 后正确移除表现
6. 任一消息解析失败时能落清晰日志

## 可直接发给 UE Agent 的 Prompt

```text
请在 UE 工程中接入 Mession 当前已经落地的场景玩家同步，只处理：
- Client_ScenePlayerEnter
- Client_ScenePlayerUpdate
- Client_ScenePlayerLeave

注意这些消息是客户端下行推送：
- MsgType=13
- 包格式为 uint16 FunctionId + uint32 PayloadSize + Payload
- 不带 CallId

FunctionId 作用域为 MClientDownlink，当前已验证值为：
- Enter=2660
- Update=43756
- Leave=1143

Enter/Update 的 payload 为：
- uint64 PlayerId
- uint16 SceneId
- float X
- float Y
- float Z

Leave 的 payload 为：
- uint64 PlayerId
- uint16 SceneId

请在 UE 里维护 RemotePlayers 映射：
- Enter 时创建或显示远端玩家
- Update 时更新远端位置
- Leave 时移除远端玩家

第一版不要扩展到完整对象复制系统。
```
