# UE Gateway Quickstart

这份文档给 UE 侧一个最短接入路径。

## 目标

先稳定打通：

1. `Client_Handshake`
2. `Client_Login`
3. `Client_Chat`

不要一上来就做完整网络层或完整玩法系统。

## 当前结论

UE -> Gateway 的正式入口已经确定为：

- `MT_FunctionCall`
- `FunctionID`
- `Payload`

不要继续扩张基于 `MessageType` 的业务 `switch`。

## 最小接入步骤

### 1. 建立 TCP 连接

连接目标：

- Gateway 默认端口 `8001`

### 2. 发送统一函数调用

先发：

- `Client_Handshake`
- `Client_Login`

登录成功后再发：

- `Client_Chat`

### 3. 接收统一下行函数调用

UE 侧下行也应按：

- `MT_FunctionCall`
- `FunctionID`
- decode
- invoke

来处理。

## 不要做的事情

- 不手写 `FunctionID` 表
- 不为了 UE 兼容修改 Server 规则
- 不继续扩张旧 `MessageType` 下行路径

## 相关文档

- [client-unified-function-call.md](/workspaces/Mession/Docs/client-unified-function-call.md)
- [function-id-rules.md](/workspaces/Mession/Docs/function-id-rules.md)
- [ue-client-downlink-function-call.md](/workspaces/Mession/Docs/ue-client-downlink-function-call.md)
