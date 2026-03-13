# 文档总览

`Readme/` 目录用于按模块拆分项目文档，和新的代码目录结构保持一致，避免根 `README.md` 过于臃肿。

## 推荐阅读顺序

如果你第一次看这个项目，可以按下面顺序：

1. `../README.md`（总览、架构图、快速开始）
2. `protocol.md`（统一包格式、客户端消息、跨服消息）
3. `core.md`（`Source/Core`：事件循环、并发、网络基础）
4. `common.md`（`Source/Common`：日志、配置、后端长连接抽象）
5. `netdriver.md`（`Source/NetDriver`：网络对象与复制系统）
6. `gateway.md`（`Source/Servers/Gateway`：客户端入口）
7. `login.md`（`Source/Servers/Login`：登录与会话校验）
8. `world.md`（`Source/Servers/World`：世界逻辑与复制）
9. `scene.md`（`Source/Servers/Scene`：场景视图）
10. `router.md`（`Source/Servers/Router`：控制面路由服务）

## 基础层模块

- [`core.md`](./core.md): `Source/Core` 内的四个子模块：
  - `Event/`：事件循环 (`MNetEventLoop`, `MEventLoop`, `MTaskEventLoop`, `IEventLoopStep`)
  - `Concurrency/`：任务与线程池 (`ThreadPool`, `TaskQueue`, `Async`, `Promise`, `ITaskRunner`)
  - `Containers/`：自定义容器（如 `RingBuffer`）
  - `Net/`：`NetCore`, `Socket`, `SocketPlatform`, `PacketCodec`, `Poll`
- [`common.md`](./common.md): `Source/Common`，日志、配置、消息工具、`MServerConnection` 等公共组件
- [`netdriver.md`](./netdriver.md): `Source/NetDriver`，运行时网络对象与复制驱动

## 服务模块

- [`gateway.md`](./gateway.md): `Source/Servers/Gateway`，客户端入口、消息分发、后端路由
- [`login.md`](./login.md): `Source/Servers/Login`，登录结果生成与 `SessionKey` 校验
- [`world.md`](./world.md): `Source/Servers/World`，玩家主状态、世界逻辑、复制与场景同步
- [`scene.md`](./scene.md): `Source/Servers/Scene`，场景实体视图与世界服协作
- [`router.md`](./router.md): `Source/Servers/Router`，控制面注册、服务发现、玩家世界服绑定

## 协议与架构

- [`protocol.md`](./protocol.md): 客户端协议、跨服协议和 Router 控制面消息
- 顶层总览：`../README.md`（项目总览、架构图、快速开始）
- 深入设计：`Docs/` 目录（事件循环、线程池、日志、协议细节等）

## 文档划分原则

- 根 `README.md`：**项目总览 + 快速开始 + 整体架构**
- `Readme/`：**按模块说明代码结构与职责**
- `Docs/`：**设计文档与演进记录**
- Router 设计与实现说明统一收敛在 `router.md` 和 `Docs/` 相关文档中
