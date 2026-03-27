# 玩法状态与对象模型

## 核心原则

当前玩法状态不再用“多个平行 DTO + 多份缓存 + 多份同步逻辑”来维护，而是以 `World` 服上的对象树为单一事实来源。

## 当前玩家对象树

玩家主状态以 `MPlayer` 为根对象。

典型子对象包括：

- `MPlayerSession`
- `MPlayerController`
- `MPlayerPawn`
- `MPlayerProfile`
- `MPlayerInventory`
- `MPlayerProgression`

这些对象都继承自 `MObject`，并通过父子关系形成一棵可遍历、可快照、可按路径定位的对象树。

## `MPlayer` 对象树

当前玩家对象树主要分工如下：

- [PlayerSession.h](/root/Mession/Source/Servers/World/Players/PlayerSession.h)
  承载登录会话、网关连接和 SessionKey 等纯连接态。
- [PlayerController.h](/root/Mession/Source/Servers/World/Players/PlayerController.h)
  承载玩家当前路由与主场景归属。
- [PlayerPawn.h](/root/Mession/Source/Servers/World/Players/PlayerPawn.h)
  承载在线实体态，例如当前在线场景与运行时生命值。
- [PlayerProfile.h](/root/Mession/Source/Servers/World/Players/PlayerProfile.h)
  承载玩家长期档案，并挂载 `Inventory` / `Progression` 子对象。

这些对象的 Persistence / Replication 含义直接写在 `MPROPERTY(...)` 标记上，而不是散落在其他配置中。

## 域标记驱动状态流

当前两个核心域：

- `Replication`
- `Persistence`

属性示例：

- `MPROPERTY(Replicated)`
- `MPROPERTY(PersistentData)`
- `MPROPERTY(PersistentData | Replicated)`

这意味着：

- 同一个对象属性可以同时参与持久化与复制
- 哪些字段该入库、哪些字段该下发，由反射系统统一发现
- 业务层不需要再维护额外的字段白名单

## 状态的归属边界

### Gateway

- 只持有客户端连接态
- 不作为玩家长期状态归属者

### World

- 是玩家主状态归属服
- 拥有对象树与脏标记消费权

### Scene

- 持有场景归属结果和轻量场景态
- 不应该复制出另一套完整玩家主档

### Mgo

- 持久化记录的读写边界
- 不承载在线领域逻辑

## 为什么要把协议结构提升为 `MSTRUCT`

此前类似对象快照记录很容易出现“手工打包 / 手工解包”的代码，维护成本高，也绕开了反射系统。

当前协议层已经使用类似 [ObjectStateMessages.h](/root/Mession/Source/Protocol/Messages/Common/ObjectStateMessages.h) 的形式：

- `FObjectPersistenceRecord`
- `MSTRUCT`
- `MPROPERTY`

这样做的意义是：

- 协议结构可以直接复用反射序列化
- 字段演进更容易统一管理
- RPC payload 层不需要为每种消息继续写专门归档逻辑

## 当前服务层分工

以 `World` 为例：

- `MWorldServer`
  负责网络入口、连接管理、运行时上下文
- `MWorldPlayerServiceEndpoint`
  负责玩家进入世界、切场、登出等业务编排
- `MPlayer` 及子对象
  负责承载真正的领域状态

这个拆分的重点是：

- `Server` 不再是万能 God Object
- `Endpoint` 承担流程
- `Player Object Tree` 承担状态

## 后续扩展建议

- 新玩法组件优先挂到对象树里，而不是平铺到 `WorldServer`
- 新的跨服消息优先进入 `Source/Protocol/Messages/<Domain>`
- 新的业务流程优先进入对应 `ServiceEndpoint` 或独立 `Workflow`
