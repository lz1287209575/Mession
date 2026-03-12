# 服务器模板与基类

各服（Gateway、Login、World、Router、Scene）共用同一套事件循环模板，由 **`MNetServerBase`**（`Common/NetServerBase.h/.cpp`）抽象并实现主循环与关服流程，子类只实现业务回调。

## 1. 基类职责

| 成员 / 方法 | 说明 |
|-------------|------|
| `MasterLoop` | **MEventLoop**（主事件循环），内挂若干 **子 EventLoop**（`IEventLoopStep`）；每帧 `MasterLoop.RunOnce()` 按注册顺序依次执行各子的 `RunOnce(timeoutMs)` |
| `TaskLoop` | `MTaskEventLoop`，子循环之一：纯任务队列，timeout 忽略 |
| `EventLoop` | `MNetEventLoop`，子循环之一：监听 + 连接 poll + 可读分发 |
| `GetTaskRunner()` | 返回 `ITaskRunner*`（即 `&TaskLoop`），供 `MAsync::Yield`、`MSequence` 投递任务 |
| `ListenerId` | 当前监听器 ID，用于 `UnregisterListener` |
| `bRunning` | 主循环是否继续；`RequestShutdown()` 置 false 并 `EventLoop.Stop()` |
| `bShutdownDone` | 是否已执行过 `Shutdown()`，避免重复清理 |
| `Run()` | 模板主循环：`RegisterListener` → `OnRunStarted()` → 注册子循环（`MasterLoop.AddStep(&TaskLoop, 0); AddStep(&EventLoop, 16)`）→ `while(bRunning){ MasterLoop.RunOnce(); TickBackends(); }` → `UnregisterListener` |
| `RequestShutdown()` | 置 `bRunning = false` 并 `EventLoop.Stop()` |
| `Shutdown()` | 若未关过则置标志、调 `ShutdownConnections()`、再 `UnregisterListener` |

## 2. 子类必须实现的接口

| 接口 | 说明 |
|------|------|
| `GetListenPort() const` | 返回监听端口（通常 `Config.ListenPort`） |
| `OnAccept(ConnId, Conn)` | 新连接到达：设非阻塞、建对端/客户端结构、`EventLoop.RegisterConnection(ConnId, Conn, OnRead, OnClose)`；Scene 可在此直接 `Conn->Close()` |
| `ShutdownConnections()` | 关服时：关闭所有连接、清空容器、断开 `MServerConnection`、必要时删复制驱动等 |

## 3. 子类可选覆盖

| 接口 | 默认行为 | 典型覆盖 |
|------|----------|----------|
| `TickBackends()` | 空 | 后端 `MServerConnection::Tick()`、定时器、会话过期、FlushSendBuffer、游戏/复制 tick |
| `OnRunStarted()` | 空 | 打 “XXX server running...” 等日志 |

## 4. 标准流程（main 侧）

1. `LoadConfig(ConfigPath)`
2. `Init(InPort)`：设 `bRunning = true`，建 `MServerConnection` 并 `Connect()`，**不**创建监听 socket
3. `Run()`：由基类执行 `RegisterListener` → 主循环 → `UnregisterListener`
4. 信号/退出时 `RequestShutdown()`，主循环退出后进程结束
5. 析构或显式 `Shutdown()`：基类调 `ShutdownConnections()` 并释放监听

## 5. 各服差异摘要

| 服务器 | 监听对象 | OnAccept 行为 | TickBackends 要点 |
|--------|----------|--------------|-------------------|
| Gateway | 客户端 | 建 `MClientConnection`，RegisterConnection(OnRead→HandleClientPacket, OnClose→登出通知 World + erase) | Router/Login/World Tick，路由查询定时 |
| Router | 对端服 | 建 `SRouterPeer`，RegisterConnection(OnRead→HandlePacket, OnClose→RemovePeer) | 仅 `++TickCounter`（租约等） |
| Login | Gateway | 建 `SGatewayPeer`，RegisterConnection(OnRead→HandleGatewayPacket, OnClose→erase) | Router Tick，会话过期清理 |
| World | Gateway 后端 | 建 `SBackendPeer`，RegisterConnection(OnRead→HandlePacket, OnClose→按连接删玩家 + erase) | Router/Login Tick，FlushSendBuffer，UpdateGameLogic，ReplicationDriver |
| Scene | 仅占端口 | `Conn->Close()`，不 RegisterConnection | Router/World Tick，场景 Tick |

## 6. 主/子事件循环结构

- **主循环**：`MEventLoop`（`Core/MEventLoop.h`），通过 `AddStep(IEventLoopStep* step, int timeoutMs)` 注册子循环，不持有所有权。
- **子循环接口**：`IEventLoopStep`（`Core/IEventLoopStep.h`），仅 `RunOnce(int timeoutMs)`；当前实现为 `MTaskEventLoop`、`MNetEventLoop`。
- 扩展：新增子循环时实现 `IEventLoopStep`，在 `Run()` 前对 `MasterLoop` 调用 `AddStep(&YourLoop, timeoutMs)` 即可。

## 7. 文件与依赖

- 基类：`Common/NetServerBase.h`、`Common/NetServerBase.cpp`（编入 `mession_common`）
- 基类依赖：`Core/MEventLoop.h`、`Core/EventLoop.h`（`MNetEventLoop`）、`Core/TaskEventLoop.h`（`MTaskEventLoop`）、`Core/IEventLoopStep.h`、`Core/ITaskRunner.h`、`Core/NetCore.h`
- 子类只需 `#include "Common/NetServerBase.h"`，继承 `MNetServerBase` 并实现上表接口即可复用整段主循环与关服逻辑；异步投递用 `GetTaskRunner()`。
