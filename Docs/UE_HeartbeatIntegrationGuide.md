# UE Heartbeat 接入说明

本文档描述当前 `Mession` 服务端已正式支持的 `Client_Heartbeat` 接入方式。

适用日期：`2026-03-31`

## 1. 结论

`Client_Heartbeat` 现在走正式客户端调用链：

- 协议类型：`MT_FunctionCall`
- `MsgType = 13`
- 目标服：`Gateway`
- 正式 `FunctionId = 8809`

不要再使用：

- `FunctionId = 46739`
- `CallId = 0`
- 旧兼容壳 `MT_Heartbeat = 11`

## 2. 请求格式

TCP 最外层仍然是 length-prefixed packet：

```text
uint32 PacketLength
uint8  MsgType
uint16 FunctionId
uint64 CallId
uint32 PayloadSize
Payload bytes...
```

其中 `Client_Heartbeat` 的固定值为：

- `MsgType = 13`
- `FunctionId = 8809`
- `CallId = 非 0，且建议递增`

请求 payload 结构：

```cpp
struct FClientHeartbeatRequest
{
    uint32 Sequence = 0;
};
```

所以 payload 只有 4 字节：

```text
uint32 Sequence
```

## 3. 响应格式

响应 payload 结构：

```cpp
struct FClientHeartbeatResponse
{
    bool bSuccess = false;
    uint32 Sequence = 0;
    uint64 ConnectionId = 0;
    string Error;
};
```

字段顺序必须按下面顺序解析：

```text
uint8  bSuccess
uint32 Sequence
uint64 ConnectionId
uint32 ErrorLength
bytes  ErrorUtf8
```

成功时通常返回：

- `bSuccess = true`
- `Sequence = 请求中的 Sequence`
- `ConnectionId != 0`
- `Error = ""`

## 4. UE 侧建议

UE 侧建议：

1. 建连成功后，每隔几秒发送一次 `Client_Heartbeat`
2. `Sequence` 用本地递增计数
3. `CallId` 也保持独立递增，不要写 0
4. 收到响应后校验：
   - `FunctionId == 8809`
   - `CallId` 匹配
   - `Sequence` 匹配

## 5. 一个最小样例

假设：

- `CallId = 7`
- `Sequence = 123`

那么逻辑上发送的是：

```text
MsgType = 13
FunctionId = 8809
CallId = 7
PayloadSize = 4
Payload = uint32(123)
```

## 6. 当前服务端实现位置

- 协议定义：`Source/Protocol/Messages/Gateway/GatewayClientMessages.h`
- Gateway 入口：`Source/Servers/Gateway/GatewayServer.h`
- Gateway 实现：`Source/Servers/Gateway/GatewayServer.cpp`

## 7. 联调结论

当前服务端已经过本地验证，heartbeat 端到端返回示例为：

```text
MsgType=13
FunctionId=8809
CallId=1
bSuccess=true
Sequence=42
ConnectionId=3
Error=""
```
