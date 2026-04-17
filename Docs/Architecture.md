# 架构总览

## 目标

Mession 当前的目标不是继续堆单点玩法，而是把游戏服务端项目收敛为一套可持续演进的基础架构。当前代码主要围绕五条主线组织：

- 运行时基础设施统一在 `Source/Common`
- 协议统一在 `Source/Protocol/Messages`
- 服务职责统一在 `Source/Servers/*`
- 领域状态统一为“对象树 + 反射属性 + 域标记”
- 客户端与服间调用统一走反射驱动的 RPC 运行时

## 目录分层

### `Source/Common`

- `Runtime/Reflect`
  反射元数据、属性读写、快照编码。
- `Runtime/Object`
  `MObject`、子对象树、对象路径、对象域快照工具。
- `Runtime/Concurrency`
  `MTaskQueue`、`MThreadPool`、`MPromise`、`MFuture`、`MCoroutine`、`MAsync`。
- `Runtime/Persistence`
  持久化记录构建、排队与 Sink 抽象。
- `Runtime/Replication`
  复制驱动、连接相关性、对象更新下发。
- `Net/Rpc`
  RPC 分发、传输、运行时上下文、负载编解码。
- `Net`
  基础连接与服务器骨架。
- `Skill`
  技能节点注册表，服务端与 UE 技能图共享 schema。

### `Source/Protocol`

协议已经按业务域拆开，不再保留大聚合头文件。

- `Messages/Auth`
- `Messages/Common`
- `Messages/Combat`
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
- `WorldClient.*`、`Player/PlayerService.*`、`Backend/*`
  服务编排、客户端入口和后端依赖装配。
- `Players/*`
  玩家对象树与玩家主状态归属。
- `Combat/*`
  仅在需要的服务器中承载战斗运行时和技能执行。

## 当前服务职责

### Gateway

- 处理客户端连接
- 统一接入客户端 `MT_FunctionCall`
- 按 `MClientManifest` 解析目标服与绑定信息
- 本地只保留极少数网关能力，例如 `Client_Echo`
- 业务 `ClientCall` 默认转发到目标服执行
- 承接 `PushClientDownlink`，把业务服下发消息推回客户端连接

### Login

- 签发与校验登录会话
- 当前职责较轻，核心是会话键与玩家映射

### World

- 持有 `MPlayer` 及其子对象树
- 负责玩家进入世界、查找、切场、登出
- 承载普通 Player RPC 的业务入口
- 对接 `Login / Scene / Router / Mgo`
- 负责将对象脏状态送入持久化子系统
- 负责场景同步相关的客户端下行
- 负责最小战斗链路的 World 侧编排和结果回写

当前 `World` 内部已经分出几条比较清晰的职责线：

- `MWorldClient`
  客户端 `Client_*` 入口，以及到 `MPlayerService` 的薄适配层。
- `MPlayerService`
  玩家进入世界、切场、登出、普通 Player RPC，以及最小战斗链路编排。
- `MPlayerManager`
  在线玩家索引、对象查找、持久化刷新与生命周期管理。
- `MPlayer` 对象树
  真正承载玩家状态和普通 Player 业务实现。

### Scene

- 负责玩家进入/离开场景
- 维护场景内玩家归属
- 负责场景玩家同步下行
- 持有轻量战斗运行时 `SceneCombatRuntime`
- 负责技能执行和战斗临时态，不直接持有完整玩家主档

### Router

- 玩家路由注册与查询
- 让 `Gateway`、`World` 可以解耦地查找玩家目标服

### Mgo

- 承接玩家状态保存/加载
- 协议层使用 `FObjectPersistenceRecord`
- 存储后端通过 `IPersistenceSink` 抽象，可替换

## 当前请求链路

### 客户端调用链

普通客户端请求当前的主链路是：

1. 客户端连接 `Gateway`
2. 发送统一 `MT_FunctionCall`
3. Gateway 依据 `MClientManifest` 判断目标服
4. World 上的 `MWorldClient` 接收 `Client_*`
5. `WorldClientPlayerList.inl` 生成的薄适配把请求转给 `MPlayerService`
6. `MPlayerService` 根据请求找到 `MPlayer` 或具体子对象
7. 子对象上的 `MFUNCTION(ServerCall)` 实现真正业务

这条链路已经覆盖：

- 查询：`Profile / Pawn / Inventory / Progression`
- 写操作：`ChangeGold / EquipItem / GrantExperience / ModifyHealth`
- 社交：`Trade / Party`

普通业务请求现在只保留一份定义：

- `FPlayer*Request` / `FWorld*Request`
  作为真正的业务 request，由 World 侧入口和服务侧共同复用。
- `FClient*`
  主要保留给客户端 response、notify，以及 `Login / Echo / Heartbeat` 这类真正的客户端侧请求。

### 跨服流程链

需要多个服务协作的流程保留在显式 workflow 中，例如：

- 登录
- 进世界
- 切场景
- 登出
- 战斗技能调用

这类流程不强行塞进普通 Player route list。

### 客户端下行链

业务服不会直接持有客户端 socket。当前下行链路是：

1. World 构造下行 payload
2. 通过 Gateway peer 调用 `PushClientDownlink`
3. Gateway 根据 `GatewayConnectionId` 推送到客户端

当前主干已经使用这条链路发送：

- `Client_ScenePlayerEnter`
- `Client_ScenePlayerUpdate`
- `Client_ScenePlayerLeave`
- 复制驱动里的对象/Actor 更新消息

## 对象状态模型

当前工程不再把“玩家状态、复制数据、持久化数据”拆成三套彼此独立的结构，而是统一为对象树。

玩家主状态以 `MPlayer` 为根，当前典型子对象包括：

- `MPlayerSession`
- `MPlayerController`
- `MPlayerPawn`
- `MPlayerProfile`
- `MPlayerInventory`
- `MPlayerProgression`
- `MPlayerCombatProfile`

属性通过 `MPROPERTY(...)` 直接声明域语义：

- `MPROPERTY(PersistentData)`
- `MPROPERTY(Replicated)`
- `MPROPERTY(PersistentData | Replicated)`

由此带来的收益是：

- 状态源唯一
- 领域切片由反射系统自动遍历
- Persistence 与 Replication 共享对象域快照能力

## 当前架构上的真实张力

虽然对象树已经成型，但当前仍有一个值得继续收口的问题：运行时状态和持久化状态的边界还没有完全压实。

当前 `SceneId`、`Health`、战斗结算状态会分散在：

- `Controller`
- `Pawn`
- `Profile`
- `Progression`
- `CombatProfile`

这也是当前文档和路线图里最值得优先处理的主线之一。

## 核心约束

### 约束 1

协议结构优先使用 `MSTRUCT + MPROPERTY`，避免继续写手动归档代码。

### 约束 2

业务流程优先放在 `MWorldClient`、`MPlayerService` 或显式 `Workflow` 中，不把大量链式逻辑塞回 `Server` 类。

### 约束 3

`Server` 负责进程边界、连接边界、运行时边界，不负责承载所有业务细节。

### 约束 4

普通 Player 业务优先落到 `MPlayer` 子对象，不要把 World endpoint 写成新的 God Object。

### 约束 5

对象属性的 Persistence / Replication 含义必须直接体现在 `MPROPERTY` 域标记上，避免散落在旁路配置里。

### 约束 6

客户端 `ClientCall` 的稳定 ID 不依赖 owner class。

- 默认使用函数名作为稳定 API 名
- 如需在重构时保持旧 ID，可显式写 `Api=...`
- Gateway、生成代码、验证脚本都以该稳定 API 名计算 ID

### 约束 7

文档、协议、工具链都应以当前代码结构为中心，不保留与主干事实冲突的旧描述
