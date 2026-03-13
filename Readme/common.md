# Common 模块（`Source/Common`）

`Source/Common` 存放跨模块复用的公共组件，位于 `Source/Core` 之上、各服务模块之下，主要负责：

- 日志与配置
- 消息读写工具
- 服务器间长连接抽象

## 目录概览

```text
Source/Common/
  Logger.h / .cpp
  LogSink.h
  Config.h
  StringUtils.h
  MessageUtils.h
  ParseArgs.h
  ServerConnection.h / .cpp
  ServerMessages.h
```

## 日志：Logger / LogSink

文件：`Logger.h/.cpp`, `LogSink.h`

- 统一日志宏：`LOG_INFO / LOG_WARN / LOG_ERROR / LOG_DEBUG`
- 简单的 sink 抽象：未来可以接入文件/控制台/远程日志
- 贯穿网络层与业务层，避免散落 `printf`/`std::cout`

## 配置与工具

- `Config.h`：配置读取和常用配置结构
- `StringUtils.h`：字符串辅助（格式化、分割、大小写等）
- `MessageUtils.h`：消息读写帮助类（读写整数、字符串等）
- `ParseArgs.h`：命令行参数解析辅助

这部分帮助服务器在启动和运行时更方便地解析配置和构造协议负载。

## 后端长连接：MServerConnection

文件：`ServerConnection.h/.cpp`

`MServerConnection` 基于 `Core/Net/MTcpConnection`，封装“服务与服务之间”的长连接逻辑：

- 主动连接 / 断开
- 非阻塞收发
- 后端握手与认证
- 心跳保活
- 自动重连
- 应用层消息回调分发

只要是“后端 ↔ 后端”的长连接，都应该优先通过 `MServerConnection` 来管理。

### 在系统中的位置

如果说 `MTcpConnection` 解决的是“完整包怎么收发”，  
那 `MServerConnection` 解决的是“后端服务之间如何长期、稳定地通信”。

使用场景包括：

- `Gateway -> Login`
- `Gateway -> World`
- `World -> Login`
- `Scene -> World`
- `Gateway/Login/World/Scene -> Router`

## ServerMessages：跨服消息封装

文件：`ServerMessages.h`

基于 `EServerMessageType` 定义了一系列跨服消息结构及其序列化逻辑，例如：

- 握手与心跳：`MT_ServerHandshake`, `MT_ServerHandshakeAck`, `MT_Heartbeat`, `MT_HeartbeatAck`
- 登录链路：`MT_PlayerLogin`, `MT_SessionValidateRequest`, `MT_SessionValidateResponse`
- 世界/场景：`MT_PlayerSwitchServer`, `MT_PlayerDataSync`, `MT_PlayerLogout`
- Router 控制面：`MT_ServerRegister`, `MT_ServerLoadReport`, `MT_RouteQuery`, `MT_RouteResponse`

所有服务器间通信都尽量经由这里统一定义，避免在各服务中散写编解码逻辑。

## 设计边界

`Source/Common` 不直接处理：

- 玩家登录状态机细节
- 世界/场景业务逻辑
- Actor 复制策略
- 选服或路由策略本身

这些职责都由具体服务模块承担（`Gateway/Login/World/Scene/Router`）。  
