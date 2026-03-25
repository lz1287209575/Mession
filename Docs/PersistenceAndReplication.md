# 持久化与复制

## 当前统一思路

Persistence 与 Replication 现在都不再各自维护一套独立“快照规则”，而是统一建立在对象域快照工具之上。

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

1. `World` 持有 `MPlayerSession` 根对象
2. Tick 时对根对象调用 `EnqueueRootIfDirty`
3. `MPersistenceSubsystem` 通过 `ObjectDomainUtils` 构造 `Persistence` 域记录
4. 每条记录分配版本号与请求号
5. 记录进入待刷队列
6. `Flush()` 调用具体 `Sink` 落地
7. 成功入队后清除对应对象的 `Persistence` 脏标记

### 记录结构

内部记录结构是 `SPersistenceRecord`，协议层对外使用 `FObjectPersistenceRecord`。二者都围绕同一组事实：

- `ObjectPath`
- `ClassName`
- `SnapshotData`

其中内部结构额外带有：

- `RootObjectId`
- `ObjectId`
- `ClassId`
- `OwnerServerId`
- `RequestId`
- `Version`

## Replication

### 核心组件

- [ReplicationDriver.cpp](/root/Mession/Source/Common/Runtime/Replication/ReplicationDriver.cpp)
- `MReplicationDriver`
- `MReplicationChannel`

### 当前流程

1. 对象注册到 `ReplicationDriver`
2. Driver 在 Tick 中遍历已注册对象
3. 若对象声明了 `Replication` 域属性，则优先走对象域快照
4. 若对象是可复制 Actor 且未声明域属性，则可退回全量 Actor 快照
5. 针对相关连接发送 `Client_OnObjectUpdate` / `Client_OnObjectCreate` / `Client_OnObjectDestroy`
6. 成功下发后清除 `Replication` 域脏标记

### 关键点

- 优先使用域标记驱动的细粒度对象快照
- 旧 Actor 复制模型目前仍保留 fallback，便于兼容
- 相关性集合在连接级别维护

## Dirty 标记的意义

统一 dirty 机制是这次重构真正的收敛点之一。

现在的目标不是：

- 持久化系统自己算变更
- 复制系统自己算变更
- 业务层再手动维护“哪些字段改了”

而是：

- 属性修改后标记所属域 dirty
- Persistence 与 Replication 都消费同一份 dirty 信息
- 不同消费者只关心自己对应的域

## 为什么这样设计

收益主要有三点：

1. 状态源唯一，减少双写和遗漏
2. 领域语义贴在属性定义处，阅读成本更低
3. Persistence / Replication 可以共享对象树遍历、对象路径解析、快照编码能力

## 当前边界

- `World` 服是当前 Persistence dirty 的主要生产者与消费发起方
- `Mgo` 是保存与加载的远端边界
- `ReplicationDriver` 目前更多用于对象更新广播与客户端下发

## 后续重点

- 继续减少复制路径里的 fallback 逻辑占比
- 让更多对象状态完整依赖 `MPROPERTY` 域标记
- 进一步统一持久化回放与对象树重建过程
