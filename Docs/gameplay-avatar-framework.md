# Gameplay Avatar Framework

这份文档定义 Mession 现在应采用的 Gameplay 分层。

## 核心结论

`Gameplay` 这一层要保留，但它只能是“领域模型层”，不能变成跨服业务垃圾场。

推荐分层：

```text
Server Application
  -> Gateway / Login / World / Scene / Router 的流程编排

Gameplay Domain
  -> Avatar / Member / Attribute / Interaction 等领域对象

Infrastructure
  -> Reflection / Snapshot / Replication / Persistence / RPC
```

## 为什么不能只放到各个 Server

如果完全把 Gameplay 拆回各个 Server：

- 运行时对象会和流程编排混在一起
- WorldServer 很快长成巨石
- UE 看到的对象模型会直接绑定 Server 内部实现
- 字段复制与持久化规则无法形成统一元数据

所以 `Gameplay` 仍然需要存在。

## 为什么又不能把所有业务都丢进 Gameplay

`Gameplay` 不该承载：

- 登录 / 鉴权 / 重连流程
- Gateway 路由与连接管理
- DB 批处理调度
- 网络发送时机

这些都应由 `Server` 或基础设施层承担。

## PlayerSession 与 PlayerAvatar

推荐至少分清两个对象：

### PlayerSession

负责：

- `PlayerId`
- `SessionKey`
- `GatewayConnectionId`
- 在线态

不负责：

- 位置
- 属性
- 交互规则
- 战斗状态

### MPlayerAvatar

负责：

- World 中的运行时实体
- 公共运行时状态
- 对 member 的聚合

不直接负责：

- DB schema
- 持久化调度
- 网络发送调度

## AvatarMember

`AvatarMember` 是能力拆分单元。

适合拆到 member 里的内容包括：

- Movement
- Attribute
- Interaction
- Inventory
- Ability

目标是让 `Avatar` 持有公共状态，
让具体能力各自收拢到 member。

## Persistence And Replication

未来推荐的属性语义应当是：

```cpp
MPROPERTY(PersistentData, RepToClient)
uint32 Level = 1;
```

这意味着：

- `Level` 属于 Avatar 运行时状态
- 修改时标记 `Persistent` dirty
- 修改时标记 `Client` dirty
- DB 和客户端复制由各自 subsystem 独立消费

而不是：

- 属性修改后立刻手写写库
- 属性修改后立刻手写发包

## 当前最值得做的下一步

不是继续扩类数量，
而是先补下面这套机制：

1. 属性 domain flags
2. per-domain dirty tracking
3. replication 消费 `Client` 域
4. persistence 消费 `Persistent` 域

这一步完成以后，再继续扩 Gameplay 业务类才是稳的。
