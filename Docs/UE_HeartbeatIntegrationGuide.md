# UE Heartbeat 接入说明

本文档描述当前 `Client_Heartbeat` 的真实接入方式。

当前心跳已经走统一客户端调用链，不应再按旧兼容壳设计。

## 当前结论

`Client_Heartbeat` 的当前路径是：

- 协议类型：`MT_FunctionCall`
- `MsgType = 13`
- 目标：`Gateway` 本地
- 当前稳定 `FunctionId = 8809`

实现位置：

- 声明：`Source/Servers/Gateway/GatewayServer.h`
- 实现：`Source/Servers/Gateway/GatewayServer.cpp`

它不会转发到 World，也不依赖登录态。

## 不要再使用的旧路径

当前 UE 接入不要再使用：

- `MT_Heartbeat = 11` 兼容壳
- `CallId = 0`
- 旧版单独心跳包约定

`NetMessages.h` 里仍保留旧枚举值是为了兼容历史路径，不代表当前推荐客户端接入方式。

## 请求格式

最外层 TCP 仍是长度前缀：

```text
uint32 PacketLength
PacketBody bytes...
```

心跳请求的 `PacketBody` 为：

```text
uint8  MsgType
uint16 FunctionId
uint64 CallId
uint32 PayloadSize
Payload bytes...
```

固定值：

- `MsgType = 13`
- `FunctionId = 8809`
- `CallId` 应为非 0 递增值

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

## 响应格式

响应 payload 结构：

```cpp
struct FClientHeartbeatResponse
{
    bool bSuccess = false;
    uint32 Sequence = 0;
    uint64 ConnectionId = 0;
    MString Error;
};
```

字段顺序：

```text
uint8  bSuccess
uint32 Sequence
uint64 ConnectionId
uint32 ErrorLength
bytes  ErrorUtf8
```

当前 Gateway 的本地实现会返回：

- `bSuccess = true`
- `Sequence = 请求中的 Sequence`
- `ConnectionId = 当前网关连接 ID`
- `Error = ""`

## 当前语义

这条心跳更适合做：

- 连接连通性确认
- 周期性保活
- 获取当前 `Gateway ConnectionId`

它不负责：

- World 业务状态检查
- 玩家是否已登录
- Session 是否仍可用于业务调用

所以 UE 侧不要把“心跳成功”等同于“登录态有效”。

## 当前字节序约束

当前传输层和 RPC 打包实现都直接按主机字节序写入。

在现有联调环境下，UE 侧应按 little-endian 兼容当前实现，包括：

- 长度前缀
- `FunctionId`
- `CallId`
- `Sequence`

## UE 侧建议接入方式

建议在连接建立后维护一个简单的心跳调度：

1. 连接成功后开始定时器
2. 每隔几秒发送一次 `Client_Heartbeat`
3. `Sequence` 本地递增
4. `CallId` 继续走统一递增分配
5. 收到响应后校验：
   - `FunctionId == 8809`
   - `CallId` 匹配
   - `Sequence` 匹配
6. 超时则记为心跳失败
7. 连续多次失败则断开并触发上层重连/重登逻辑

## 推荐日志

至少打印：

- 发包时：
  - `FunctionId`
  - `CallId`
  - `Sequence`
- 收包时：
  - `CallId`
  - `Sequence`
  - `ConnectionId`
  - `Error`

这样后面排查“连接断了”“请求没回”“回包错配”会容易很多。

## 一个最小样例

假设：

- `CallId = 7`
- `Sequence = 123`

则逻辑上发送：

```text
MsgType = 13
FunctionId = 8809
CallId = 7
PayloadSize = 4
Payload = uint32(123)
```

若连接正常，当前期望响应：

```text
bSuccess = true
Sequence = 123
ConnectionId = non-zero
Error = ""
```

## 与当前验证脚本的关系

当前 `Scripts/validate.py` 主线覆盖的重点是登录、查询、写操作、场景同步和错误透传。

心跳目前没有纳入那条主线验证脚本的核心步骤，但服务端实现已经在 Gateway 本地稳定存在，适合作为 UE 连接层的基础能力先接入。

## 可直接发给 UE Agent 的 Prompt

```text
请在 UE 工程中按当前主线协议接入 Client_Heartbeat。

要求：
- 走 MT_FunctionCall，不走旧 MT_Heartbeat 兼容壳
- MsgType=13
- FunctionId=8809
- 请求格式为 uint16 FunctionId + uint64 CallId + uint32 PayloadSize + Payload
- Payload 为 uint32 Sequence
- Response 为 bool bSuccess + uint32 Sequence + uint64 ConnectionId + string Error
- 当前协议按 little-endian 兼容现有实现

请在连接成功后定时发送心跳，并校验返回的 FunctionId、CallId、Sequence 是否匹配。
不要把心跳成功等同于登录成功。
```
