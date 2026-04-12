# 持久化与复制

## 当前统一思路

Persistence 与 Replication 不再各自维护一套独立的快照规则，而是统一建立在对象域快照工具之上。

核心入口在：

- [ObjectDomainUtils.h](/root/Mession/Source/Common/Runtime/Object/ObjectDomainUtils.h)

这里负责：

- 递归遍历类与父类属性
- 按域标记筛选属性
- 构建对象域快照
- 递归遍历对象子树
- 生成带 `ObjectPath` 与 `ClassName` 的快照记录

## Persistence

### 核心组件

- [PersistenceSubsystem.h](/root/Mession/Source/Common/Runtime/Persistence/PersistenceSubsystem.h)
- `IPersistenceSink`
- `MNoopPersistenceSink`
- `MMongoPersistenceSink`

### 当前流程

1. `World` 持有玩家对象树根对象
2. 业务改动通过 `MPROPERTY` 域标记和 dirty 机制标识 Persistence 变更
3. `MPersistenceSubsystem` 通过 `ObjectDomainUtils` 构造 `Persistence` 域记录
4. 每条记录分配版本号与请求号
5. 记录进入待刷队列
6. `Flush()` 调用具体 `Sink` 落地
7. 成功进入待刷队列后清除对应对象的 Persistence dirty

当前子系统本身不直接理解玩家业务，它只消费对象域快照。

### 记录结构

内部记录结构是 `SPersistenceRecord`，协议层对外使用 `FObjectPersistenceRecord`。二者围绕同一组事实：

- `ObjectPath`
- `ClassName`
- `SnapshotData`

内部结构额外带有：

- `RootObjectId`
- `ObjectId`
- `ClassId`
- `OwnerServerId`
- `RequestId`
- `Version`

### 当前现实边界

当前 Persistence 在设计上已经统一，但在业务语义上仍受玩家状态边界影响。

例如当前登出或战斗结算前，仍可能需要先把运行时状态从：

- `Pawn`
- `Controller`
- `CombatProfile`

桥接回：

- `Profile`
- `Progression`

然后再进入持久化记录构建。这也是当前状态模型还没完全收口的表现。

## Replication

### 核心组件

- [ReplicationDriver.h](/root/Mession/Source/Common/Runtime/Replication/ReplicationDriver.h)
- [ReplicationDriver.cpp](/root/Mession/Source/Common/Runtime/Replication/ReplicationDriver.cpp)
- `MReplicationDriver`
- `MReplicationChannel`

### 当前流程

1. 对象注册到 `ReplicationDriver`
2. Driver 在 Tick 中遍历已注册对象
3. 如果类声明了 `Replication` 域属性，则优先走对象域快照
4. 如果对象是可复制 Actor 且未声明域属性，则退回 Actor fallback 快照
5. 按连接相关性发送对象更新
6. 成功下发后清除 `Replication` 域 dirty

### 当前下发消息

复制驱动当前主要使用这些客户端 downlink：

- `Client_OnObjectCreate`
- `Client_OnObjectUpdate`
- `Client_OnObjectDestroy`

旧 Actor 兼容消息入口也仍然保留：

- `Client_OnActorCreate`
- `Client_OnActorUpdate`
- `Client_OnActorDestroy`

这说明当前复制路径已经偏向对象域，但 Actor fallback 还没有完全退出。

## 场景同步与复制的关系

当前仓库里还有一条业务级下行同步链，主要用于场景玩家状态广播：

- `Client_ScenePlayerEnter`
- `Client_ScenePlayerUpdate`
- `Client_ScenePlayerLeave`

它和 `ReplicationDriver` 不是一回事：

- `ReplicationDriver`
  面向通用对象复制
- 场景同步下行
  面向玩家场景态和业务语义更明确的消息

当前两条链路并存，这是现阶段合理的现实，而不是设计错误。

## Dirty 标记的意义

统一 dirty 机制是这次重构真正的收敛点之一。

目标不是：

- 持久化系统自己算变更
- 复制系统自己算变更
- 业务层再手动维护“哪些字段改了”

而是：

- 属性修改后标记所属域 dirty
- Persistence 与 Replication 消费同一份 dirty 信息
- 不同消费者只关心自己对应的域

## 为什么这样设计

收益主要有三点：

1. 状态源唯一，减少双写和遗漏
2. 领域语义贴在属性定义处，阅读成本更低
3. Persistence / Replication 可以共享对象树遍历、对象路径解析、快照编码能力

## 当前仍待继续收敛的点

- 复制路径里的 Actor fallback 仍然存在
- 玩家状态边界未完全压实，导致持久化前仍可能需要桥接同步
- Persistence / Replication 专项自动化回归还不够系统
- 通用对象复制和业务场景同步的边界说明仍值得继续补强

## 后续重点

- 继续减少复制路径里的 fallback 逻辑占比
- 让更多对象状态完整依赖 `MPROPERTY` 域标记
- 进一步统一持久化回放与对象树重建过程
- 给 Persistence / Replication 补更细粒度的回归测试
