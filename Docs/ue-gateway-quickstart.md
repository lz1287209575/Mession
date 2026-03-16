# UE Client -> Gateway Quickstart

## Goal

这份文档用于给 UE 侧或实现 Agent 快速对接当前 Mession Gateway。

目标不是一开始就把 UE 客户端做成完整网络层，而是先稳定打通：

- `UE Client -> Gateway`
- `UE Client -> Gateway -> Login`
- `UE Client -> Gateway -> World`

当前最推荐先打通三条消息：

1. `MT_Handshake`
2. `MT_Login`
3. `MT_Chat`

## Current Architecture Decision

当前客户端接入的正式方向已经确定：

- `Client <-> Gateway` 使用 `MessageType + PayloadStruct`
- `Gateway <-> Login/World/Router` 使用内部反射/RPC 运行时

也就是说：

- UE 客户端可以使用 UE 自己的反射系统来声明“客户端消息协议”
- 但不应该直接复用服务端内部 `MT_RPC` 作为长期正式客户端协议

当前关于 `MT_RPC` 的结论：

- `Server <-> Server` 的 `MT_RPC` 保留为内部 RPC 通道
- `Client -> Gateway` 的 `MT_RPC` 仅作为受控兼容路径保留
- UE 新功能不要继续建立在客户端 `MT_RPC` 上

## What UE Should Declare

UE 侧建议声明的是“客户端协议消息”，不是“内部服务 RPC”。

推荐使用：

- `UENUM(BlueprintType)` 定义客户端消息类型
- `USTRUCT(BlueprintType)` 定义客户端 payload
- `UCLASS()` + `UFUNCTION(BlueprintCallable)` 暴露发送接口

推荐分层：

1. UE 反射层：声明消息和 payload
2. 编解码层：将 UE struct 编成网络 payload
3. 发送层：统一发 `Length + MsgType + Payload`

## Current Gateway Entries

当前 Gateway 已经接好的客户端入口在：

- [GatewayServer.h](/workspaces/Mession/Source/Servers/Gateway/GatewayServer.h)

主要入口如下：

```cpp
MFUNCTION(Client, Message=MT_Handshake, Reliable=true)
void Client_Handshake(uint64 ClientConnectionId, const SPlayerIdPayload& Request);

MFUNCTION(Client, Message=MT_Login, Reliable=true, Route=Login, Auth=None, Wrap=LoginRpcOrLegacy)
void Client_Login(uint64 ClientConnectionId, const SPlayerIdPayload& Request);

MFUNCTION(Client, Message=MT_Chat, Reliable=true, Route=RouterResolved, Target=World, Auth=Required, Wrap=PlayerClientSync)
void Client_Chat(uint64 ClientConnectionId, const SClientChatPayload& ChatPayload);

MFUNCTION(Client, Message=MT_Heartbeat, Reliable=false)
void Client_Heartbeat(uint64 ClientConnectionId, const SHeartbeatMessage& Heartbeat);
```

## Wire Format

当前客户端包格式：

```text
Length(4 bytes, little-endian)
MsgType(1 byte)
Payload(N bytes)
```

说明：

- `Length` 表示后续 `MsgType + Payload` 的总长度
- 当前这几条客户端消息按小端写即可，和现有脚本验证一致

## Minimal Message Set

对应：

- [NetMessages.h](/workspaces/Mession/Source/Messages/NetMessages.h)
- [ServerMessages.h](/workspaces/Mession/Source/Common/ServerMessages.h)

当前最小消息类型：

```cpp
MT_Login = 1
MT_LoginResponse = 2
MT_Handshake = 3
MT_PlayerMove = 5
MT_Chat = 10
MT_Heartbeat = 11
```

### 1. MT_Handshake

当前 Gateway 侧复用了 `SPlayerIdPayload`：

```text
[MsgType=3][PlayerId(8)]
```

用途：

- 确认 `GatewayLocal` 本地入口打通
- 不再转发到 Login

### 2. MT_Login

当前也复用了 `SPlayerIdPayload`：

```text
[MsgType=1][PlayerId(8)]
```

登录成功响应：

```text
[MsgType=2][SessionKey(4)][PlayerId(8)]
```

### 3. MT_Chat

当前 payload 对应 `SClientChatPayload`：

```text
[MsgType=10][MessageLen(2)][MessageBytes(UTF-8)]
```

说明：

- 字符串长度当前写 `uint16`
- 字符串内容按 UTF-8 字节写入

### 4. MT_Heartbeat

当前 payload 对应 `SHeartbeatMessage`：

```text
[MsgType=11][Sequence(4)]
```

用途：

- 验证 Gateway 本地入口
- 验证接入层状态观测

## Recommended UE Reflection Style

下面这种风格最适合 UE 侧快速实现：

```cpp
UENUM(BlueprintType)
enum class EMessionClientMessage : uint8
{
    Login = 1,
    LoginResponse = 2,
    Handshake = 3,
    PlayerMove = 5,
    Chat = 10,
    Heartbeat = 11,
};

USTRUCT(BlueprintType)
struct FMessionPlayerIdRequest
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    uint64 PlayerId = 0;
};

USTRUCT(BlueprintType)
struct FMessionChatRequest
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString Message;
};
```

然后通过一个统一的发送类暴露：

```cpp
UFUNCTION(BlueprintCallable)
bool SendHandshake(uint64 PlayerId);

UFUNCTION(BlueprintCallable)
bool SendLogin(uint64 PlayerId);

UFUNCTION(BlueprintCallable)
bool SendChat(const FString& Message);
```

## Minimal Implementation Plan For UE Agent

建议 UE/Agent 按下面顺序实现：

### Step 1. 建一个最小 Gateway Client 类

建议名字类似：

- `UMessionGatewayClient`
- 或 `UMessionGatewayClientSubsystem`

至少提供：

- `Connect(Host, Port)`
- `Disconnect()`
- `SendHandshake(PlayerId)`
- `SendLogin(PlayerId)`
- `SendChat(Message)`
- `TickReceive()`

### Step 2. 实现统一发包格式

统一做：

- `WriteUint16`
- `WriteUint32`
- `WriteUint64`
- `WriteUtf8String`
- `SendPacket(MsgType, Payload)`

### Step 3. 先只支持登录响应解析

先解析：

- `MT_LoginResponse`

先不要急着把所有下行消息都做完。

### Step 4. 联调顺序

按下面顺序验证：

1. 连接 Gateway
2. 发送 `MT_Handshake`
3. 发送 `MT_Login`
4. 收到 `MT_LoginResponse`
5. 发送 `MT_Chat`

这样最容易确认：

- UE 到 Gateway 通了
- GatewayLocal 通了
- Login 路径通了
- 登录后业务消息通了

## Do Not Do Yet

当前明确不建议 UE 侧做的事情：

- 不要直接把客户端功能建立在 `Client MT_RPC` 上
- 不要把对象 id / function id / 内部反射函数布局暴露给 UE 公网协议层
- 不要让 UE 直接去“声明服务端内部函数”

如果 UE 需要技能、交互、施法、背包等功能，请定义明确客户端消息，例如：

- `MT_UseSkill`
- `MT_Interact`
- `MT_CastAbility`
- `MT_InventoryAction`

然后让 Gateway / World 在服务端内部再决定是否转为反射调用。

## Debug / Validation

联调时建议直接观察 Gateway debug HTTP：

- `clientHandshakeCount`
- `lastClientHandshakePlayerId`
- `clientHeartbeatCount`
- `lastClientHeartbeatSequence`
- `legacyClientRpcCount`
- `rejectedClientFallbackCount`

当前脚本验证可参考：

- [Scripts/validate.py](/workspaces/Mession/Scripts/validate.py)

尤其是：

- `Test 1: Handshake local route`
- `Test 2: Multi-player login`
- `Test 6: Chat route`
- `Test 7: Heartbeat local route`

## Handoff Notes For UE Agent

如果把这份文档交给 UE Agent，实现目标建议写成：

1. 在 UE 中实现一个最小 TCP Gateway client
2. 按本文协议发送 `MT_Handshake`
3. 按本文协议发送 `MT_Login`
4. 解析 `MT_LoginResponse`
5. 按本文协议发送 `MT_Chat`
6. 不要使用客户端 `MT_RPC`

完成标准建议写成：

- 可以连接 `127.0.0.1:8001`
- 能发 `MT_Handshake`
- 能发 `MT_Login` 并收到 `MT_LoginResponse`
- 能发 `MT_Chat`
- 不引入客户端 `MT_RPC`
