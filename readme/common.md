# Common 模块

## 作用

`Common/` 存放跨模块复用的公共组件，当前主要包括：

- `Logger.h`
- `ServerConnection.h` / `ServerConnection.cpp`

这一层位于 `Core/` 之上、业务模块之下，负责把一些“通用但带协议语义”的能力抽出来。

## `Logger`

日志模块负责统一输出格式和日志级别。

当前价值主要在于：

- 统一 `INFO / WARN / ERROR / DEBUG`
- 让网络层和业务层使用一致的日志方式
- 避免各模块自己散写 `printf`

## `ServerConnection`

`MServerConnection` 是后端服务之间的长连接抽象，封装了：

- 主动连接和断开
- 非阻塞收发
- 后端握手
- 心跳保活
- 应用层消息回调
- 自动重连

## 在系统中的定位

如果说 `MTcpConnection` 解决的是“完整包怎么收发”，  
那 `MServerConnection` 解决的是“后端服务怎么长期稳定通信”。

它被用于：

- `Gateway -> Login`
- `Gateway -> World`
- `World -> Login`
- `Scene -> World`
- `Login / World / Scene / Gateway -> Router`

## 当前协议职责

`ServerConnection` 主要负责这些公共消息：

- `MT_ServerHandshake / MT_ServerHandshakeAck`
- `MT_Heartbeat / MT_HeartbeatAck`
- Router 控制面消息
- 业务层跨服消息的统一转发入口

## 设计边界

`Common/` 不直接处理：

- 玩家登录状态机
- 场景逻辑
- Actor 复制策略
- 路由选服策略

这些都应该由具体服务模块承担。
