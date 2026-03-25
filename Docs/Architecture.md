# 架构总览

## 目标

Mession 当前的目标是把游戏服务端项目收敛为一套可持续演进的基础架构，而不是继续堆单点功能。整个工程现在围绕四条主线组织：

- 运行时基础设施统一在 `Source/Common`
- 协议统一在 `Source/Protocol/Messages`
- 服务器职责统一在 `Source/Servers/*`
- 领域状态统一为“对象树 + 反射属性 + 域标记”

## 目录分层

### `Source/Common`

- `Runtime/Reflect`
  反射元数据、属性读写、结构快照。
- `Runtime/Object`
  `MObject`、子对象树、对象路径、对象域快照工具。
- `Runtime/Concurrency`
  `MTaskQueue`、`MThreadPool`、`MPromise`、`MFuture`、`MCoroutine`、`MAsync`。
- `Runtime/Persistence`
  持久化记录构建与 Sink 抽象。
- `Runtime/Replication`
  复制驱动、对象/Actor 更新下发。
- `Net/Rpc`
  RPC 分发、传输、运行时上下文、负载编解码。
- `Net`
  基础连接与服务器骨架。

### `Source/Protocol`

协议已经按业务域拆开，不再保留“一个大聚合头文件”。

- `Messages/Auth`
- `Messages/Common`
- `Messages/Gateway`
- `Messages/Mgo`
- `Messages/Router`
- `Messages/Scene`
- `Messages/World`

### `Source/Servers`

每个服务器目录都尽量遵循同一结构：

- `Server.h/.cpp`
  监听、连接管理、运行时上下文、对外 RPC 入口。
- `Rpc/*`
  指向其他服务器的强类型 RPC 包装。
- `Services/*Endpoint*`
  编排业务流程，不直接承担底层网络细节。
- `Domain/*`
  领域对象与对象树，仅在确有状态归属时出现。

## 当前服务职责

### Gateway

- 处理客户端连接
- 处理 `Client_*` 请求
- 作为客户端协议与服间协议的边界层
- 当前依赖 `Login` 与 `World`

### Login

- 签发与校验登录会话
- 当前状态较轻，核心是会话键与玩家映射

### World

- 持有 `MPlayerSession` 及其子对象树
- 负责玩家进入世界、查找、切场、登出
- 对接 `Login / Scene / Router / Mgo`
- 负责将对象脏状态送入持久化子系统

### Scene

- 负责玩家进入/离开场景
- 管理轻量场景分配状态

### Router

- 玩家路由注册与查询
- 让 `Gateway`、`World` 可以解耦地查找玩家目标服

### Mgo

- 承接玩家状态保存/加载
- 协议层使用 `FObjectPersistenceRecord`
- 存储后端通过 `IPersistenceSink` 抽象，可替换

## 对象状态模型

当前工程不再把“玩家状态、复制数据、持久化数据”拆成三套彼此独立的结构，而是统一为对象树：

- 根对象示例：`MPlayerSession`
- 子对象示例：`MPlayerAvatar`、`MInventoryComponent`、`MAttributeComponent`
- 属性通过 `MPROPERTY(...)` 声明领域含义

典型标记：

- `MPROPERTY(PersistentData)`
- `MPROPERTY(Replicated)`
- `MPROPERTY(PersistentData | Replicated)`

由此带来的核心收益：

- 状态只维护一份
- 领域切片由反射系统自动遍历
- Persistence 与 Replication 可以复用同一个对象域快照能力

## 核心约束

### 约束 1

协议结构优先使用 `MSTRUCT + MPROPERTY`，避免为跨服消息继续写手动归档代码。

### 约束 2

业务流程优先放在 `ServiceEndpoint` 或 `Workflow` 中，不把大量链式逻辑塞回 `Server` 类。

### 约束 3

`Server` 负责进程边界、连接边界、运行时边界，不负责承载所有业务细节。

### 约束 4

对象属性的 Persistence / Replication 含义必须直接体现在 `MPROPERTY` 域标记上，避免散落在额外注册表或旁路配置里。

### 约束 5

文档、协议、工具链都应以当前代码结构为中心，不再回退到旧目录组织。
