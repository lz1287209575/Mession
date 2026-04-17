# 玩法状态与对象模型

## 核心原则

当前玩法状态不再用“多个平行 DTO + 多份缓存 + 多份同步逻辑”来维护，而是以 `World` 服上的对象树为单一事实来源。

## 当前玩家对象树

玩家主状态以 `MPlayer` 为根对象。

当前典型子对象包括：

- `MPlayerSession`
- `MPlayerController`
- `MPlayerPawn`
- `MPlayerProfile`
- `MPlayerInventory`
- `MPlayerProgression`
- `MPlayerCombatProfile`

这些对象都继承自 `MObject`，并通过父子关系形成一棵可遍历、可快照、可按路径定位的对象树。

## 当前各子对象职责

### `MPlayerSession`

- 登录会话
- 网关连接
- `SessionKey`
- 纯连接态，不负责持久化玩法状态

### `MPlayerController`

- 当前玩家路由
- 当前主场景归属
- 更偏“控制与路由”语义

### `MPlayerPawn`

- 场景驻留运行时状态
- 当前位置
- 当前在线场景
- 当前运行时生命值

`Pawn` 当前是最接近“在线实体态”的对象。

### `MPlayerProfile`

- 玩家长期画像根
- 当前持久化场景归属
- 挂载 `Inventory` 与 `Progression`
- 负责把查询结果聚合成面向客户端的资料视图

### `MPlayerInventory`

- 金币
- 当前装备
- 背包类读写操作入口

### `MPlayerProgression`

- 等级
- 经验
- 当前持久化生命值
- 成长相关读写操作入口

### `MPlayerCombatProfile`

- 战斗静态属性
- `BaseAttack / BaseDefense / MaxHealth / PrimarySkillId`
- 最近一次战斗结果快照，如 `LastResolvedHealth`
- 为 Scene 战斗 Avatar 构造输入快照

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
- 业务层不需要再维护额外字段白名单

## 当前状态归属边界

### Gateway

- 只持有客户端连接态
- 不作为玩家长期状态归属者

### World

- 是玩家主状态归属服
- 持有对象树
- 负责脏标记消费、业务编排、战斗结果回写

### Scene

- 持有场景归属与轻量场景态
- 持有轻量战斗运行时
- 不应该复制出另一套完整玩家主档

### Mgo

- 持久化记录读写边界
- 不承载在线领域逻辑

## 当前已经打通的玩法链路

当前主干已经能跑通这些状态相关链路：

- 登录进入世界
- 查找玩家和切场
- 移动与场景同步
- 查询 `Profile / Pawn / Inventory / Progression`
- 写 `Gold / EquippedItem / Experience / Health`
- 双玩家同场景同步
- 最小战斗链路 `Client_CastSkill`
- 登出后重登恢复

这说明当前对象树不仅是设计结构，已经承担真实业务读写和状态恢复。

## 当前还没完全收口的地方

虽然对象树已经成型，但玩家状态边界还没有完全压实。

当前 `SceneId` 和 `Health` 仍会分散在多个对象：

- `Controller.SceneId`
- `Pawn.SceneId`
- `Profile.CurrentSceneId`
- `Progression.Health`
- `Pawn.Health`
- `CombatProfile.LastResolvedHealth`

当前已经明确下来的边界是：

- `Profile.CurrentSceneId`
  是离线恢复和持久化视角下的主场景状态
- `Controller.SceneId`
  是运行时 route / 调度视角下的场景状态，不再需要单独承担持久化桥
- `Pawn.SceneId`
  是场景存在态镜像，只在玩家真正驻留 Scene 时有效
- `Progression.Health`
  是玩家长期血量主状态
- `Pawn.Health`
  是场景运行时镜像，跟随主状态更新
- `CombatProfile.LastResolvedHealth`
  是战斗结算快照，不再反向充当血量主状态
- `CombatProfile.LastResolvedSceneId`
  与 `LastResolvedHealth` 一样，属于最近一次战斗结算快照，只服务审计、诊断和回放
- `ExperienceReward`
  属于成长结果，已经开始直接落到 `Progression`，不应继续挂在战斗快照层里充当“待处理状态”

代码里仍有一些桥接逻辑，例如：

- 登录恢复后统一回填 `Controller`，而不是在加载前抢先写默认值
- 修改生命值时统一走 `Player::ApplyResolvedHealth()`，由 `Progression` 持有主状态、`Pawn` 只在 spawned 时同步镜像
- `SyncRuntimeState()` 现在只负责收口 `SceneId`，不再重复把血量回写到 `Progression`
- 战斗结算时先记录 `CombatProfile` 快照，再把规范化后的血量落回主状态

目前代码中 `LastResolvedSceneId / LastResolvedHealth` 只被写入，不被业务逻辑读取。
这意味着它们当前已经是纯快照字段，后续不应再被重新引入为主状态判断条件。

这部分当前是主干里最需要继续收口的状态问题。

## 为什么协议结构提升为 `MSTRUCT`

类似对象快照记录、战斗请求、玩家查询请求，如果继续用手工打包 / 解包，会直接绕开反射系统。

当前协议层已经统一使用 `MSTRUCT + MPROPERTY`，这样做的意义是：

- 协议结构直接复用反射序列化
- 字段演进更容易统一管理
- RPC payload 层不需要为每种消息继续写专门归档逻辑

## 当前服务层分工

以 `World` 为例：

- `MWorldServer`
  负责连接、运行时上下文、跨模块装配。
- `MWorldClient`
  承担客户端入口，以及到 `MPlayerService` 的薄适配。
- `MPlayerService`
  承担玩家 workflow、普通 Player RPC 与最小战斗编排。
- `MPlayerManager`
  承担在线玩家对象索引、子对象查找与生命周期管理。
- `MPlayer` 及子对象
  承担真正的领域状态和普通 Player 业务逻辑。

这个拆分的重点是：

- `Server` 不再是万能 God Object
- `Client / Service / Manager` 各自承担清晰边界
- `Player Object Tree` 承担状态

## 当前扩展建议

- 新玩法组件优先挂到对象树里，而不是平铺到 `WorldServer`
- 新的普通 Player 业务优先落到 Player 子对象
- 新的跨服流程优先进入 `MWorldClient`、`MPlayerService` 或独立 `Workflow`
- 新的战斗属性优先判断它属于“长期主状态”“运行时镜像”还是“战斗快照”

## 现在最值得继续做的事

如果继续推进当前仓库，玩法状态层面最值得优先做的是：

1. 收口 `Profile / Pawn / Progression / CombatProfile` 的边界
2. 减少运行时状态和持久化状态之间的桥接同步
3. 让战斗结果、登出回写、重登恢复都走更稳定的状态模型
