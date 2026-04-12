# UE 客户端接入指南

本文档面向 UE 侧实现同学，只描述当前仓库已经落地并可联调的客户端接入方式。

当前客户端统一直连 `GatewayServer`，协议主入口为 `MT_FunctionCall`。登录已经不是唯一目标能力；当前主线还包含查询、写操作、场景同步和基础战斗调用。

## 当前客户端 API

`Build/Generated/MClientManifest.generated.h` 当前登记了 16 个客户端入口：

- Gateway 本地：
  - `Client_Echo`
  - `Client_Heartbeat`
- World 转发：
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

UE 第一阶段不需要一次性接全量能力，但文档和代码骨架应按这组 API 设计，而不是按旧的“只做登录四步”设计。

## 本地服务拓扑

默认本地端口如下：

- `GatewayServer`: `8001`
- `LoginServer`: `8002`
- `WorldServer`: `8003`
- `SceneServer`: `8004`
- `RouterServer`: `8005`
- `MgoServer`: `8006`

客户端只连接 `GatewayServer`。

## 推荐联调方式

最直接的服务端验证方式：

```bash
python3 Scripts/validate.py --build-dir Build --no-build
```

当前 `validate.py` 已覆盖：

- `Client_Login`
- `Client_FindPlayer`
- `Client_SwitchScene`
- `Client_Move`
- `Client_QueryProfile`
- `Client_QueryPawn`
- `Client_QueryInventory`
- `Client_QueryProgression`
- `Client_ChangeGold`
- `Client_EquipItem`
- `Client_GrantExperience`
- `Client_ModifyHealth`
- `Client_CastSkill`
- `Client_Logout`
- `Client_ScenePlayerEnter`
- `Client_ScenePlayerUpdate`
- `Client_ScenePlayerLeave`
- 登出后重登
- 参数绑定错误和后端不可用错误透传

这说明 UE 接入的目标应是当前主线协议，而不是旧版最小 smoke test。

如果手工起服，需要注意完整登录链路依赖 `MgoServer`。直接使用 `validate.py` 最省事。

## TCP 包格式

最外层是长度前缀包：

```text
uint32 PacketLength
PacketBody bytes...
```

`PacketLength` 不包含自身 4 字节。

当前实现使用主机字节序直接写入。以现有 Windows/Linux x86_64 联调环境来说，UE 侧应按 little-endian 兼容当前实现。

## 客户端请求包格式

客户端上行统一使用 `MT_FunctionCall = 13`：

```text
uint8  MsgType
uint16 FunctionId
uint64 CallId
uint32 PayloadSize
Payload bytes...
```

约束：

- `MsgType` 固定为 `13`
- `FunctionId` 使用稳定 ID
- `CallId` 必须由客户端生成，建议从 `1` 开始递增
- 响应会带回相同的 `FunctionId` 和 `CallId`

## 客户端下行包格式

当前场景同步下行也使用 `MT_FunctionCall = 13`，但没有 `CallId`：

```text
uint8  MsgType
uint16 FunctionId
uint32 PayloadSize
Payload bytes...
```

UE 侧必须区分：

- 请求/响应包：有 `CallId`
- Downlink 推送：无 `CallId`

## FunctionId 规则

客户端请求使用：

```cpp
ComputeStableReflectId("MClientApi", FunctionName)
```

客户端下行使用：

```cpp
ComputeStableReflectId("MClientDownlink", FunctionName)
```

仓库里已有可复用实现：

- `Source/Common/Runtime/Reflect/Class.h`
- `Scripts/validate.py`

推荐 UE 侧直接实现同样的稳定 ID 算法，避免长期手写 magic number。

当前已验证的常用 ID 如下：

- `Client_Heartbeat = 8809`
- `Client_Login = 528`
- `Client_FindPlayer = 20722`
- `Client_QueryProfile = 3609`
- `Client_QueryPawn = 8455`
- `Client_QueryInventory = 42507`
- `Client_QueryProgression = 40214`
- `Client_Logout = 60160`
- `Client_SwitchScene = 53343`
- `Client_ChangeGold = 25343`
- `Client_EquipItem = 53234`
- `Client_GrantExperience = 26406`
- `Client_ModifyHealth = 29243`
- `Client_CastSkill = 30785`
- `Client_ScenePlayerEnter = 2660`
- `Client_ScenePlayerUpdate = 43756`
- `Client_ScenePlayerLeave = 1143`

## 反射序列化规则

当前客户端负载不是 protobuf，也不是 JSON，而是反射系统按字段声明顺序顺排写入。

UE 最少需要支持：

- `bool` -> `uint8`
- `uint16`
- `uint32`
- `uint64`
- `int32`
- `float`
- `MString` -> `uint32 ByteLen + UTF-8 bytes`

关键点：

- 没有 tag
- 没有字段名
- 没有 padding 协议层补齐
- 完全依赖字段顺序
- 结构顺序以头文件里的 `MPROPERTY()` 顺序为准

主要消息定义位于：

- `Source/Protocol/Messages/Gateway/GatewayClientMessages.h`
- `Source/Protocol/Messages/Combat/CombatClientMessages.h`
- `Source/Protocol/Messages/Scene/SceneSyncMessages.h`

## 当前建议接入顺序

### 第一阶段

先打通传输和最基础状态：

1. TCP 连接与 length-prefix 拆包
2. `MT_FunctionCall` 请求/响应编解码
3. `Client_Heartbeat`
4. `Client_Login`
5. `Client_FindPlayer`

### 第二阶段

补齐查询链路：

1. `Client_QueryProfile`
2. `Client_QueryPawn`
3. `Client_QueryInventory`
4. `Client_QueryProgression`

### 第三阶段

补齐写操作和场景行为：

1. `Client_Move`
2. `Client_SwitchScene`
3. `Client_ChangeGold`
4. `Client_EquipItem`
5. `Client_GrantExperience`
6. `Client_ModifyHealth`

### 第四阶段

补齐下行和战斗：

1. `Client_ScenePlayerEnter`
2. `Client_ScenePlayerUpdate`
3. `Client_ScenePlayerLeave`
4. `Client_CastSkill`

这样拆分的原因是：前两阶段足够验证连接、编解码、状态读取；后三阶段再接入更具体的玩法和表现层。

## 登录请求与响应

### `FClientLoginRequest`

```cpp
struct FClientLoginRequest
{
    uint64 PlayerId = 0;
};
```

### `FClientLoginResponse`

```cpp
struct FClientLoginResponse
{
    bool bSuccess = false;
    uint64 PlayerId = 0;
    uint32 SessionKey = 0;
    MString Error;
};
```

登录成功后建议立刻追加：

- `Client_FindPlayer`
- 或 `Client_QueryProfile`

作为二次确认。

## 当前登录的真实含义

当前 `Client_Login` 并不是只拿一个会话号。

实际链路会进入 World 登录工作流，并完成：

- `Login.IssueSession`
- `Login.ValidateSessionCall`
- `Mgo.LoadPlayer`
- World 内 `MPlayer` 创建或恢复
- `Scene.EnterScene`
- `Router.ApplyRoute`

所以“登录成功”意味着该玩家已经进入当前世界态，而不是一个纯粹的网关鉴权成功。

## Heartbeat 的位置

`Client_Heartbeat` 当前是 Gateway 本地调用，定义和实现位于：

- `Source/Servers/Gateway/GatewayServer.h`
- `Source/Servers/Gateway/GatewayServer.cpp`

它不会转发到 World。

UE 侧推荐在连接稳定后定时发送心跳，但当前主线验证并不依赖心跳先打通才可登录。

## 场景同步的边界

当前适合 UE 首先接入的下行只有：

- `Client_ScenePlayerEnter`
- `Client_ScenePlayerUpdate`
- `Client_ScenePlayerLeave`

仓库里也存在：

- `Client_OnObjectCreate`
- `Client_OnObjectUpdate`
- `Client_OnObjectDestroy`

但这些更偏对象复制/对象同步入口，不建议放在 UE 第一轮联调里一起实现。

## 常见错误处理

UE 侧至少要区分三类失败：

1. TCP 断连
2. 同 `CallId` 响应超时
3. 业务错误响应

当前常见错误码包括：

- `player_id_required`
- `gateway_connection_id_required`
- `login_server_unavailable`
- `world_server_missing`
- `client_call_param_binding_failed`
- `client_route_backend_unavailable`
- `server_call_disconnected`
- `server_call_send_failed`

Gateway 在不少失败场景下会按目标响应结构反射填充默认错误响应，所以 UE 不要只盯着断线，也要正确解析业务失败包。

## UE 侧最小可用模块

推荐至少拆成这几个模块：

- `FMessionTcpClient`
  - 连接、发送、拆包
- `FMessionProtocolCodec`
  - `MT_FunctionCall` 请求/响应/下行编解码
- `FMessionReflectCodec`
  - 基础字段与结构编解码
- `UMessionGatewaySubsystem`
  - 连接、心跳、请求分发、会话状态
- `UMessionPlayerSyncSubsystem`
  - 远端玩家缓存与三条场景下行处理

## 建议验收项

UE 接入完成后，至少应能完成以下联调结果：

1. 连接 `127.0.0.1:8001`
2. 发送 `Client_Login`
3. 成功解析 `FClientLoginResponse`
4. 自动追加 `Client_FindPlayer`
5. 成功拉取 `Client_QueryProfile`
6. 成功处理 `Client_ScenePlayerEnter / Update / Leave`
7. 失败场景能明确打印错误码和 `CallId`

## 可直接发给 UE Agent 的实现说明

```text
请在 UE 工程中实现当前 Mession Gateway 客户端接入，不要按旧的最小登录样板设计。

连接方式：
- 客户端直连 127.0.0.1:8001
- TCP 使用 uint32 长度前缀
- 当前协议按 little-endian 兼容现有实现

请求格式：
- MsgType=13
- uint16 FunctionId
- uint64 CallId
- uint32 PayloadSize
- Payload

下行格式：
- MsgType=13
- uint16 FunctionId
- uint32 PayloadSize
- Payload
- 注意下行没有 CallId

第一阶段请实现：
- Heartbeat
- Login
- FindPlayer
- QueryProfile

第二阶段请实现：
- QueryPawn
- QueryInventory
- QueryProgression
- Move
- SwitchScene

第三阶段请实现：
- ChangeGold
- EquipItem
- GrantExperience
- ModifyHealth
- CastSkill
- ScenePlayerEnter/Update/Leave

请把代码拆成传输层、协议层、反射编解码层、Gateway 子系统、PlayerSync 子系统，并在日志中输出 FunctionId、CallId、PlayerId、SessionKey、Error。
```
