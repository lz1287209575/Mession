# UE 最小登录接入文档

本文档面向 UE 侧实现同学，目标是让 UE Agent 按文档即可把当前 `Mession` 协议接入到 Unreal 工程中，并跑通“连接 Gateway -> 登录 -> 确认玩家进入世界”的最小闭环。

文档内容严格以当前仓库代码为准，适用日期为 `2026-03-30`。

## 1. 目标范围

本期只做最小可运行登录链路，不做账号系统、不做 UI 美化、不做心跳重连策略扩展、不做复制流接入。

UE 侧最小交付结果：

1. 能通过 TCP 连接 `GatewayServer`
2. 能发送统一 `MT_FunctionCall` 格式的 `Client_Login`
3. 能正确解析 `FClientLoginResponse`
4. 能在登录成功后追加一个 `Client_FindPlayer` 或 `Client_QueryProfile` 作为 smoke test
5. 能在 UE 日志中明确打印成功 / 失败原因

当前服务端登录协议非常轻：

- 登录请求只需要 `PlayerId`
- 当前没有账号密码、JWT、刷新令牌、签名握手
- 当前没有登录前置 `Handshake`
- `SessionKey` 会在登录成功后返回，但当前客户端后续请求里并不需要带回这个 `SessionKey`

也就是说，UE 侧不要额外脑补 Web 登录体系；先严格按当前协议落地。

## 2. 当前服务拓扑与端口

默认本地端口如下：

- `GatewayServer`: `8001`
- `LoginServer`: `8002`
- `WorldServer`: `8003`
- `SceneServer`: `8004`
- `RouterServer`: `8005`
- `MgoServer`: `8006`

客户端只直连 `GatewayServer`。

## 3. 本地联调启动方式

### 3.1 推荐验证命令

如果只是确认服务端当前链路可用，直接执行：

```bash
python3 Scripts/validate.py --build-dir Build --no-build
```

这个脚本会自动启动：

`Router -> Mgo -> Login -> World -> Scene -> Gateway`

并验证：

- `Client_Login`
- `Client_FindPlayer`
- `Client_SwitchScene`
- `Client_Logout`

我在 `2026-03-30` 本地执行过该命令，验证通过，当前登录链路可用。

### 3.2 手工起服注意事项

`python3 Scripts/servers.py start --build-dir Build` 只会启动：

`Router -> Login -> World -> Scene -> Gateway`

它当前不会启动 `MgoServer`，而完整登录流程依赖 `MgoServer` 参与玩家加载。所以 UE 联调时：

- 要么直接用 `validate.py` 做一键验证
- 要么手工先启动 `Bin/MgoServer`，再启动其他服务

## 4. 客户端 TCP 包格式

### 4.1 最外层包头

所有客户端 TCP 包都使用长度前缀：

```text
+----------------------+-------------------+
| uint32 PacketLength  | PacketBody bytes  |
+----------------------+-------------------+
```

说明：

- `PacketLength` 不包含这 4 个字节自身
- 当前实现是原样 `memcpy` 写入整数
- 在当前目标环境下，UE 侧必须按 little-endian 处理

### 4.2 当前最小登录使用的消息类型

客户端统一走：

- `MT_FunctionCall = 13`

所以登录包体格式是：

```text
+----------------+-------------------+----------------+---------------------+
| uint8 MsgType  | uint16 FunctionId | uint64 CallId  | uint32 PayloadSize  |
+----------------+-------------------+----------------+---------------------+
| Payload bytes...                                                        |
+-------------------------------------------------------------------------+
```

约束：

- `MsgType` 固定为 `13`
- `FunctionId` 是客户端 API 稳定 ID
- `CallId` 由客户端自己生成，建议从 `1` 开始递增
- 响应包会原样带回同一个 `FunctionId` 和 `CallId`

## 5. FunctionId 计算规则

### 5.1 计算规则

客户端函数 ID 使用固定作用域 `MClientApi`，算法是 FNV 风格哈希后折叠到 `uint16`：

```cpp
uint16 ComputeStableClientFunctionId(const char* ClientApiName)
{
    return ComputeStableReflectId("MClientApi", ClientApiName);
}
```

展开后等价于：

1. 用 `2166136261` 作为初始值
2. 依次混入 `"MClientApi"`
3. 再混入 `':'`
4. 再混入 `':'`
5. 再混入函数名，例如 `"Client_Login"`
6. 最后做 `((Hash >> 16) ^ (Hash & 0xFFFF)) & 0xFFFF`
7. 如果结果为 `0`，则改成 `1`

### 5.2 当前最小链路建议直接写死的 FunctionId

为减少 UE 首次接入成本，第一版可以直接写死以下函数 ID：

- `Client_Login = 528`
- `Client_FindPlayer = 20722`
- `Client_QueryProfile = 3609`
- `Client_QueryPawn = 8455`
- `Client_SwitchScene = 53343`
- `Client_Logout = 60160`

建议同时把算法也实现出来，后续扩协议时直接复用。

## 6. 反射序列化规则

当前 `Mession` 协议结构体序列化不是 protobuf，也不是 JSON，而是反射系统按属性声明顺序顺排写入。

UE 侧最小只需要支持这几种基础类型：

- `bool` -> `uint8`，`0` 为 false，非 `0` 为 true
- `uint32` -> 4 字节 little-endian
- `uint64` -> 8 字节 little-endian
- `float` -> 4 字节 IEEE754 little-endian
- `MString` -> `uint32 ByteLen + UTF-8 bytes`

关键点：

- 没有字段 tag
- 没有字段名
- 没有对齐填充
- 完全依赖字段顺序
- 结构体字段顺序以头文件中 `MPROPERTY()` 声明顺序为准

## 7. 登录请求与响应结构

### 7.1 `FClientLoginRequest`

定义位置：

- `Source/Protocol/Messages/Gateway/GatewayClientMessages.h`

结构：

```cpp
struct FClientLoginRequest
{
    uint64 PlayerId = 0;
};
```

编码后 payload 只有 8 个字节：

```text
+-------------------+
| uint64 PlayerId   |
+-------------------+
```

### 7.2 `FClientLoginResponse`

结构：

```cpp
struct FClientLoginResponse
{
    bool bSuccess = false;
    uint64 PlayerId = 0;
    uint32 SessionKey = 0;
    MString Error;
};
```

编码顺序：

```text
+---------------+-------------------+--------------------+--------------------+
| uint8 Success | uint64 PlayerId   | uint32 SessionKey  | string Error       |
+---------------+-------------------+--------------------+--------------------+
```

其中 `string Error` 继续展开为：

```text
+--------------------+----------------------+
| uint32 ErrorLength | Error UTF-8 bytes... |
+--------------------+----------------------+
```

成功时典型返回：

- `bSuccess = true`
- `PlayerId = 请求中的 PlayerId`
- `SessionKey = 非 0`
- `Error = ""`

失败时典型返回：

- `bSuccess = false`
- `SessionKey = 0`
- `Error = 错误码`

### 7.3 一个可直接对照的登录请求样例

假设：

- `PlayerId = 12345`
- `CallId = 1`
- `FunctionId = 528`

那么完整 TCP 发送字节应为：

```text
17 00 00 00
0d
10 02
01 00 00 00 00 00 00 00
08 00 00 00
39 30 00 00 00 00 00 00
```

按字段拆解：

- `17 00 00 00` -> `PacketLength = 23`
- `0d` -> `MsgType = 13`
- `10 02` -> `FunctionId = 528`
- `01 00 00 00 00 00 00 00` -> `CallId = 1`
- `08 00 00 00` -> `PayloadSize = 8`
- `39 30 00 00 00 00 00 00` -> `PlayerId = 12345`

## 8. 最小登录时序

### 8.1 客户端视角

UE 侧最小流程：

1. 连接 `GatewayServer:8001`
2. 生成一个递增 `CallId`
3. 构造 `Client_Login` 请求
4. 发送 `MT_FunctionCall`
5. 等待同 `FunctionId`、同 `CallId` 的响应
6. 解析 `FClientLoginResponse`
7. 若成功，缓存：
   - `PlayerId`
   - `SessionKey`
8. 立刻再发一个 `Client_FindPlayer` 或 `Client_QueryProfile` 做二次确认

### 8.2 服务端内部真实流程

服务端当前登录链路是：

1. 客户端发 `Client_Login`
2. `GatewayServer` 按 `FunctionId` 把调用转发到 `World`
3. `World.Client_Login`
4. `World -> Login.IssueSession`
5. `World -> PlayerEnterWorld`
6. `PlayerEnterWorld -> Login.ValidateSessionCall`
7. `PlayerEnterWorld -> Mgo.LoadPlayer`
8. `World` 创建或恢复 `MPlayer`
9. `World -> Scene.EnterScene`
10. `World -> Router.ApplyRoute`
11. 返回 `FClientLoginResponse`

所以“登录成功”的真实含义不是只拿到一个 `SessionKey`，而是玩家已经被放进当前世界状态里。

## 9. 登录后的推荐二次确认

最推荐的 smoke test 是 `Client_FindPlayer`。

### 9.1 `Client_FindPlayer`

请求结构：

```cpp
struct FClientFindPlayerRequest
{
    uint64 PlayerId = 0;
};
```

响应结构：

```cpp
struct FClientFindPlayerResponse
{
    bool bFound = false;
    uint64 PlayerId = 0;
    uint64 GatewayConnectionId = 0;
    uint32 SceneId = 0;
    MString Error;
};
```

第一登录成功后，期望值通常为：

- `bFound = true`
- `PlayerId = 登录 PlayerId`
- `GatewayConnectionId != 0`
- `SceneId = 1` 或该玩家持久化恢复出的场景
- `Error = ""`

### 9.2 `Client_QueryProfile`

如果希望 UI 上顺便拿到一组可展示数据，可以在登录后再调 `Client_QueryProfile`。

对于全新玩家，当前默认值来自代码初始值：

- `CurrentSceneId = 1`
- `Gold = 0`
- `EquippedItem = "starter_sword"`
- `Level = 1`
- `Experience = 0`
- `Health = 100`

## 10. 错误返回约定

Gateway 在转发失败或后端报错时，会尝试按目标响应结构“反射填充”一个错误响应。

对于 `FClientLoginResponse` 来说，失败时通常表现为：

- `bSuccess = false`
- `PlayerId = 0` 或业务层写入的值
- `SessionKey = 0`
- `Error = 错误码`

UE 侧处理建议：

1. 先看 TCP 是否断开
2. 再看是否收到了同 `CallId` 的响应
3. 最后看响应里的 `bSuccess` / `Error`

当前常见错误码包括：

- `player_id_required`
- `gateway_connection_id_required`
- `login_server_unavailable`
- `world_player_service_missing`
- `client_route_backend_unavailable`
- `session_invalid`

## 11. UE 侧推荐模块拆分

建议 UE Agent 按下面 4 个最小模块落地，不要把所有逻辑揉进一个 Actor：

### 11.1 `FMessionTcpClient`

职责：

- 建立 / 关闭 TCP 连接
- 发送 length-prefixed packet
- 累积接收缓冲并拆包
- 将完整 `PacketBody` 抛给上层

### 11.2 `FMessionProtocolCodec`

职责：

- 编码 `MT_FunctionCall`
- 解码 `MT_FunctionCall`
- 管理 `CallId`
- 按 `FunctionId + CallId` 关联请求与响应

建议数据结构：

- `uint64 NextCallId`
- `TMap<uint64, FPendingRequest>` 或 `TMap<FCallKey, FPendingRequest>`

如果一个连接上同一时刻只跑很少请求，也可以先简单按 `CallId` 建表。

### 11.3 `FMessionReflectCodec`

职责：

- 编码 / 解码当前需要的基础字段类型
- 先只支持：
  - `bool`
  - `uint32`
  - `uint64`
  - `float`
  - `FString <-> UTF-8`

### 11.4 `UMessionLoginSubsystem` 或 `UMessionGatewayClient`

职责：

- 提供 `Login(PlayerId)`
- 登录成功后缓存：
  - `PlayerId`
  - `SessionKey`
- 提供 `FindPlayer(PlayerId)` / `QueryProfile(PlayerId)`
- 向蓝图或 UI 广播登录结果

## 12. UE 侧最小实现顺序

建议严格按下面顺序做：

1. 只做 TCP 连接和收包
2. 实现 length prefix 拆包
3. 实现 `MT_FunctionCall` 封包 / 解包
4. 写死 `Client_Login = 528`
5. 写死 `FClientLoginRequest` / `FClientLoginResponse` 编解码
6. 打通登录
7. 再补 `Client_FindPlayer = 20722`
8. 再补 `Client_QueryProfile = 3609`

不要一上来就实现全量协议反射，不需要。

## 13. UE 伪代码

### 13.1 FunctionId

```cpp
static uint16 ComputeStableClientFunctionId(const FString& ApiName)
{
    uint32 Hash = 2166136261u;

    auto MixAnsi = [&Hash](const char* Text)
    {
        while (Text && *Text)
        {
            Hash ^= static_cast<uint8>(*Text);
            Hash *= 16777619u;
            ++Text;
        }
    };

    MixAnsi("MClientApi");
    Hash ^= static_cast<uint8>(':');
    Hash *= 16777619u;
    Hash ^= static_cast<uint8>(':');
    Hash *= 16777619u;

    FTCHARToUTF8 NameUtf8(*ApiName);
    MixAnsi(NameUtf8.Get());

    uint16 Folded = static_cast<uint16>((Hash >> 16) ^ (Hash & 0xFFFFu));
    return Folded == 0 ? 1 : Folded;
}
```

### 13.2 编码登录请求

```cpp
TArray<uint8> BuildLoginCallPacket(uint64 PlayerId, uint64 CallId)
{
    const uint8 MsgType = 13;
    const uint16 FunctionId = 528; // Client_Login

    TArray<uint8> Payload;
    WriteUInt64LE(Payload, PlayerId);

    TArray<uint8> Body;
    WriteUInt8(Body, MsgType);
    WriteUInt16LE(Body, FunctionId);
    WriteUInt64LE(Body, CallId);
    WriteUInt32LE(Body, Payload.Num());
    Body.Append(Payload);

    TArray<uint8> Packet;
    WriteUInt32LE(Packet, Body.Num());
    Packet.Append(Body);
    return Packet;
}
```

### 13.3 解码登录响应

```cpp
bool ParseLoginResponse(const TArray<uint8>& Payload, FLoginResult& OutResult)
{
    FByteReader Reader(Payload);

    OutResult.bSuccess = Reader.ReadBool();
    OutResult.PlayerId = Reader.ReadUInt64();
    OutResult.SessionKey = Reader.ReadUInt32();
    OutResult.Error = Reader.ReadUtf8String();

    return Reader.IsValid() && Reader.IsFullyConsumed();
}
```

## 14. 当前协议的几个重要现实约束

### 14.1 当前协议实际按 little-endian 运行

虽然工程里有“协议使用网络字节序”的注释，但当前客户端 RPC、长度前缀、反射归档实现都是直接 `memcpy`，所以 UE 侧必须按 little-endian 兼容当前实现。

这点非常重要。

### 14.2 `SessionKey` 当前只需要缓存，不需要回传

当前最小客户端请求：

- `Client_FindPlayer`
- `Client_QueryProfile`
- `Client_Move`
- `Client_SwitchScene`
- `Client_Logout`

都仍然只需要 `PlayerId`，不需要把 `SessionKey` 带回服务端。

所以第一版 UE 接入只要：

- 登录成功后保存 `PlayerId`
- 保存 `SessionKey` 以便后续扩展

就够了。

### 14.3 重连后要重新登录

因为当前世界态里绑定了：

- `GatewayConnectionId`
- `SessionKey`

如果 TCP 连接断开，UE 侧应视为这条登录态失效，重新走 `Client_Login`，不要假设可以继续复用旧连接上的状态。

## 15. 最小验收标准

UE Agent 完成后，至少满足以下验收项：

1. 能配置 `Host` / `Port` / `PlayerId`
2. 点击或调用登录后，能成功收到 `FClientLoginResponse`
3. 日志打印：
   - `PlayerId`
   - `SessionKey`
   - `CallId`
4. 登录成功后自动发 `Client_FindPlayer`
5. 若 `bFound == true`，日志打印：
   - `GatewayConnectionId`
   - `SceneId`
6. 任一步失败时，错误日志能打印：
   - TCP 失败 / 超时 / 解析失败 / 业务错误码

## 16. 推荐调试顺序

联调时推荐按这个顺序排查：

1. 先确认 `GatewayServer:8001` 可以连通
2. 再确认 length prefix 是否正确
3. 再确认 `MsgType` 是否是 `13`
4. 再确认 `FunctionId` 是否是 `528`
5. 再确认 `CallId` 和响应是否匹配
6. 再确认 payload 是否按 little-endian 编码
7. 最后看服务端日志：
   - `Build/validate_logs/GatewayServer.log`
   - `Build/validate_logs/WorldServer.log`
   - `Build/validate_logs/LoginServer.log`

## 17. 可直接交给 UE Agent 的实施指令

下面这段可以直接转给 UE 侧 Agent：

```text
请在 UE 工程中实现一个最小 Mession Gateway Client，只做 TCP + length-prefixed packet + MT_FunctionCall。
目标是连 127.0.0.1:8001，发送 Client_Login(FunctionId=528)，请求 payload 为 uint64 PlayerId 的 little-endian 编码，
并解析 FClientLoginResponse: bool bSuccess + uint64 PlayerId + uint32 SessionKey + UTF-8 string Error。
登录成功后继续发送 Client_FindPlayer(FunctionId=20722) 做二次确认，并打印 bFound / GatewayConnectionId / SceneId。
注意当前协议必须按 little-endian 处理，响应包会回同一个 FunctionId 和 CallId。
第一版不要实现账号密码、心跳、重连、全量反射系统，只做最小登录闭环。
```
