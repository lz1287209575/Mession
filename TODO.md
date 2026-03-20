# TODO

这份文档只记录当前正在推进的核心重构决议。

## 当前目标

从 `WorldServer` 的登录完成链路开始，重新梳理玩家运行时对象模型。

## 登录与玩家创建

### 1. 重做 World 登录落地流程

当前问题：

- `FinalizePlayerLogin()` 职责过重，不只是 finalization
- `AddPlayer()` 同时承担 session 创建、avatar 创建、replication 绑定、场景广播
- 当前顺序是先创建玩家进世界，再异步从 DB/Mgo 补快照，容易出现默认值先进场的问题

目标方案：

- 登录流程拆成明确阶段：
- `ValidateSession`
- `LoadPlayerState`
- `CreateRuntimePlayer`
- `EnterWorld`

完成标准：

- 玩家进入世界前，运行时对象已经拿到正确的持久化数据
- `WorldServer` 只做编排，不继续堆积对象拼装细节

### 2. 移除 `AddPlayer(Name, ...)` 这种创建方式

当前问题：

- `AddPlayer(uint64 PlayerId, const MString& Name, ...)` 不合理
- 正常玩家名字应该从 DB 加载，而不是由 `WorldServer` 临时拼 `"Player" + PlayerId`
- `MPlayerSession::Name` 与 `MPlayerAvatar::DisplayName` 存在职责重叠

目标方案：

- 移除 `AddPlayer` 的 `Name` 入参
- 玩家名字等持久化业务字段从 DB 加载
- `MPlayerSession` 不再承载名字这类持久化业务字段
- 持久化名字统一落到 `MPlayerAvatar` 或后续独立 `Profile` 对象

完成标准：

- 新玩家与老玩家都不再依赖 `WorldServer` 本地拼名字
- 登录建角与加载角色的字段来源清晰一致

## MObject / GC

### 3. 在 Common 层补正式对象系统

当前问题：

- `MObject` 目前只有反射/脏标记能力，没有正式对象树
- 代码里仍有 `new MPlayerAvatar()` 这类裸分配
- 没有 `Outer`、默认子对象、RootSet、GC 遍历入口

目标方案：

- 在 `Common` 层补齐对象机制
- `MObject` 增加：
- `Outer`
- `Children`
- `ObjectFlags`
- `AddToRoot()` / `RemoveFromRoot()`
- `VisitReferencedObjects(...)`

完成标准：

- 运行时对象不再依赖裸 `new/delete`
- 对象关系能表达 owner / child / root
- 后续 GC 和对象生命周期管理有统一基础

### 4. 增加 `NewMObject` / `CreateDefaultSubObject`

当前问题：

- 运行时对象创建没有统一入口
- 默认子对象初始化完全靠手写过程式逻辑

目标方案：

- 提供类似 UE 风格的统一接口：
- `NewMObject<T>(Outer, Name, ...)`
- `CreateDefaultSubObject<T>(Owner, Name, ...)`

完成标准：

- `Avatar`、`Session`、`AvatarMember` 都通过统一对象工厂创建
- 创建时自动建立对象树关系与基础初始化

## 玩家运行时模型

### 5. 以 `MPlayerAvatar` 作为玩家根对象

当前问题：

- `Players` 现在存的是 `TMap<uint64, MPlayerSession>`
- `MPlayerSession` 是局部变量先构造，再塞进 map，再回填 `Avatar`
- 这属于过程式拼装，不是清晰的对象拥有关系

目标方案：

- `MPlayerAvatar` 作为玩家根运行时对象
- `WorldServer::Players` 改为按 `PlayerId -> MPlayerAvatar*` 管理
- 通过 Avatar 获取玩家运行时子对象与成员对象

完成标准：

- 玩家对象入口唯一
- 查玩家、复制玩家、持久化玩家都围绕 `MPlayerAvatar` 展开

### 6. `MPlayerSession` 改为 Avatar 的子对象

当前问题：

- `MPlayerSession` 当前既像持久化对象，又像在线会话对象
- 它不应该以局部值对象方式单独声明

目标方案：

- `MPlayerSession` 保持 `MCLASS()`
- 在创建 `MPlayerAvatar` 时通过 `CreateDefaultSubObject` 一起创建
- `MPlayerSession` 只承载运行时在线态：
- `GatewayConnectionId`
- `SessionKey`
- `bOnline`
- 场景上下文等运行时数据

完成标准：

- `PlayerSession` 的职责收敛为在线态/连接态
- `PlayerAvatar` 与 `PlayerSession` 拥有关系清晰

### 7. `AvatarMember` 逐步纳入子对象体系

当前问题：

- 现在 `Members` 还是 `TUniquePtr` 手工管理
- 未来不利于统一 GC、反射遍历、生命周期控制

目标方案：

- 后续把 `AttributeMember`、`InventoryMember`、`MovementMember` 等纳入默认子对象体系
- 由 `MPlayerAvatar` 在初始化时创建默认 member

完成标准：

- Avatar 及其成员对象的生命周期统一
- 减少手工回收与手工绑定代码

## 推荐推进顺序

1. 先补 `Common` 层最小对象系统：`Outer`、`Children`、Root、`NewMObject`
2. 再改 `MPlayerAvatar` / `MPlayerSession` 的拥有关系
3. 再改 `WorldServer` 登录流程，调整为先加载持久化数据，再进入世界
4. 最后把 `AvatarMember` 逐步接到子对象体系里
