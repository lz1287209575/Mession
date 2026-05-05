# ET 与 NoahGameFrame 借鉴分析

> 调研时间：2026-05-04
> 调研目标：为 Mession 识别可借鉴的架构能力

---

## 一、能力全景对照

| 维度 | ET | NoahGameFrame | Mession 当前 |
|------|-----|---------------|-------------|
| 事件总线 | EventSystem（全局 + 对象级） | NFEventModule（全局 + 对象 + 类级别） | MEventBus（全局）✅ |
| 定时器 | TimerComponent（时间轮） | NFScheduleModule（multiset） | 分散在各连接里 |
| 协程 / Fiber | C# async/await | 无内置，纯线程池 | POSIX ucontext / Windows 空实现 |
| Actor 模型 | Actor / Component | NFActor + NFIComponent | MPlayerCommandRuntime（per-player strand）✅ |
| 对象系统 | Entity / Component | NFIObject（属性 + Record + 事件回调） | MObject + Player 子对象树 ✅ |
| Scene 分层 | Scene / Zone | Scene → SceneGroup → Cell（网格） | SceneServer 单层，无分层 |
| 属性变更事件 | Property / Record 回调 | AddPropertyCallBack | 无 |
| 配置数据系统 | Element（配置表） | NFIElementModule（XML） | 无 |
| 组件生命周期 | Awake / Init / Execute / Shut | Awake / Init / AfterInit / Execute / Shut / Finalize | MObject 无分层 |
| 热重载 | C# dynamic | Plugin ReLoad | 无 |
| 网络层 | KCP / TCP | TCP + protobuf | poll() TCP |
| 反射驱动 RPC | — | protobuf | MCLASS / MFUNCTION ✅ 已有且是亮点 |

---

## 二、ET 核心设计分析

### 2.1 EventSystem

ET 的事件系统是其最核心的解耦机制，支持两种订阅方式：

**全局事件** — 任意位置订阅，任意位置发布：
```csharp
// 订阅
EventSystem.Instance.Subscribe<FPlayerLogin>(OnPlayerLogin);

// 发布
EventSystem.Instance Publish(new FPlayerLogin { PlayerId = 1, Level = 10 });
```

**对象级事件** — 绑定到特定 Entity 实例：
```csharp
// 在某个 Entity 上注册
instance.EventSystem.Subscribe(instance.Id, OnHpChanged);

// 在 Entity 内部触发，只有该 Entity 的订阅者收到
instance.EventSystem.Publish(instance.Id, new FHPChanged { ... });
```

**关键设计：**
- 每个事件类型有全局唯一的 `uint64` TypeId，通过 `static` 变量保证
- 对象级事件通过 `instanceId + typeId` 联合索引，O(1) 定位
- 订阅时可以指定执行顺序（priority），实现有序广播

**对 Mession 的借鉴：** MEventBus 目前只有全局订阅，可以增加对象级事件支持。

### 2.2 Actor / Component 模型

ET 的 Actor 是真正的线程亲和单元：

```csharp
// 创建 Actor，绑定到一个 Fiber（纤程）
var actor = EntityFactory.CreateWithId<A>(actorId);
actor.AddComponent<MailBoxComponent>();

// 向 Actor 发消息
ActorMessageSenderComponent.Instance.Send(actorId, new G2A_Test { ... });
```

每个 Actor 有自己的 MailBox（ mailbox_component），消息按顺序处理。不同 Actor 之间完全隔离，实现了"没有锁的并发"。

**对 Mession 的借鉴：** MPlayerCommandRuntime 已经实现了 per-player 串行化，但缺少跨进程 Actor 消息路由能力。

### 2.3 Fiber / 协程调度

ET 的协程模型基于 C# 的 async/await，但实现了多层 Fiber 调度：

- **RootFiber** — 主逻辑 Fiber
- **InnerFiber** — 场景 Fiber（每个场景一个）
- **OneThreadInnerComponent** — 强制单线程执行，保证无锁

```csharp
// 等待另一个 Fiber 的结果
await Coroutine_lock.WaitAsync(lockId);
try { ... }
finally { Coroutine_lock.Release(lockId); }
```

### 2.4 Scene / Zone 管理

ET 的 Scene 是逻辑隔离单元，Zone 是子区域。场景切换通过 `SceneChangeHelper` 封装，支持跨进程跳转。

---

## 三、NoahGameFrame 核心设计分析

### 3.1 Plugin / Module 架构

NGF 是最值得 Mession 直接参考的框架，因为它也是 C++ 实现。

**Plugin 层级（部署单元）：**
```cpp
// 每个插件是一个 DLL/so，生命周期独立
class NFIPlugin {
    virtual void Install() = 0;   // 注册所有模块
    virtual void Uninstall() = 0; // 反注册
};

// 插件管理器
class NFIPluginManager {
    template<typename T>
    T* FindModule();  // 全局访问任意模块
};
```

**Module 层级（功能单元）：**
```cpp
class NFIModule {
    virtual bool Awake()       { return true; }  // 构造后
    virtual bool Init()        { return true; }  // 模块初始化
    virtual bool AfterInit()   { return true; }  // 所有模块 Init 后
    virtual bool Execute()     { return true; }  // 每帧 tick
    virtual bool Shut()       { return true; }  // 关闭前
    virtual bool Finalize()   { return true; }  // 析构前
};
```

**执行顺序严格保证：** Awake → Init → AfterInit → CheckConfig → ReadyExecute → [loop Execute] → BeforeShut → Shut → Finalize

**对 Mession 的借鉴：** 这个分层生命周期模式非常成熟，可以直接引入 MObject 或 MWorldServer 子模块。

### 3.2 Event / Property 回调系统

NGF 的事件和回调是其可扩展性的核心：

```cpp
// 全局事件（模块级）
m_pEventModule->DoEvent(EVENT_ID, NFDataList() << arg1 << arg2);

// 对象级事件（绑定到某个 NFGUID）
m_pEventModule->AddEventCallBack(playerID, EVENT_ID, this, &MyModule::OnPlayerEvent);
m_pEventModule->DoEvent(playerID, EVENT_ID, NFDataList() << value);

// 属性变更回调（最强大）
pObject->AddPropertyCallBack("Hello", this, &MyModule::OnPropertyChanged);
// 回调签名：NFGUID self, 属性名, 旧值, 新值, 原因
```

**NGF 的 Class 级别事件（COE_* 生命周期钩子）：**
```cpp
COE_CREATE_NODATA, COE_CREATE_BEFORE_ATTACHDATA, COE_CREATE_LOADDATA,
COE_CREATE_AFTER_ATTACHDATA, COE_CREATE_BEFORE_EFFECT, COE_CREATE_EFFECTDATA,
COE_CREATE_AFTER_EFFECT, COE_CREATE_READY, COE_CREATE_HASDATA, COE_CREATE_FINISH,
COE_CREATE_CLIENT_FINISH, COE_BEFOREDESTROY, COE_DESTROY
```

**对 Mession 的借鉴：** MObject 的 dirty tracking 机制已经存在，但缺少外部可订阅的属性变化回调。这是低风险、高收益的增量改进。

### 3.3 Scene / SceneGroup / Cell 三层空间

NGF 的空间分区是其最复杂的子系统之一：

```
Scene (int sceneId)           // 游戏世界，例如"主城"
  └─ SceneGroup (int groupId) // 实例，例如"主城-1服"
       └─ Cell (grid)          // 视野网格，AOI 计算
            └─ NFIObject      // 具体实体
```

**CellModule（网格视野）：**
- 每个 Cell 默认 10 单位宽
- 对象进入 Cell 时触发 `OnObjectEntry`
- 对象离开 Cell 时触发 `OnObjectLeave`
- 跨 Cell 移动触发移动事件，用于下游位置同步

**对 Mession 的借鉴：** Mession 当前 SceneServer 是单层，所有玩家共享一个空间。引入 Group 层可以实现实例化副本；引入 Cell 层可以实现 AOI（Area of Interest，视野剔除）。

### 3.4 NFIObject 的 Record 系统

NGF 的 Record 是一个二维表格数据结构，类似 DataTable：

```cpp
// 创建 Record（列定义）
AddRecord("PlayerItem", ...);

// 设置格子
SetRecordInt("PlayerItem", row, col, value);

// 行变化事件
AddRecordCallBack("PlayerItem", this, &MyModule::OnRecordChanged);
```

**对 Mession 的借鉴：** Mession 当前没有 Record 等价物。物品格子、背包等使用固定字段。如果需要动态表格（如成就列表、排行榜），Record 是个好参考。

### 3.5 NFScheduleModule 定时器

```cpp
// 注册心跳定时器（重复 10 次，间隔 5 秒）
AddSchedule(self, "OnHeartBeat", cb, 5.0f, 10);

// 注册一次性定时器（count = 1）
AddSchedule(self, "OnDelayedTask", cb, 3.0f, 1);

// 按名字移除
RemoveSchedule(self, "OnHeartBeat");

// 按 OwnerId 清理（防止内存泄漏）
RemoveSchedule(self);  // 该对象所有定时器全部移除
```

实现：使用 `std::multiset<TickElement>` 按触发时间排序，`Execute()` 每帧检查队首。

**对 Mession 的借鉴：** 这正是 TimerManager 要实现的核心功能。NGF 的 `CancelAllTimers(ownerId)` 防止对象销毁后回调访问已释放内存，是必须有的能力。

### 3.6 NFActor 消息队列

```cpp
class NFIActor : public NFIActor {
    NFGUID id;
    NFQueue<NFActorMessage> mMessageQueue;  // 线程安全的无锁队列

    virtual bool SendMsg(const NFActorMessage& message);
    // 发消息后，Actor 的 Execute() 在另一个线程中处理
};
```

**与 ET Actor 的区别：** NGF 的 Actor 主要是计算卸载工具（"使用多核 CPU"）；ET 的 Actor 是分布式消息目标（跨进程）。

---

## 四、可借鉴功能优先级

### 当前阶段可直接做（风险低，收益明确）

**1. per-object 事件作用域（ET + NGF）**
- NGF 每个对象有独立事件表，MEventBus 目前只有全局订阅
- 增加 `ObjectId + EventTypeId` 联合索引即可
- 纯增量，不破坏现有 API

**2. 属性变更事件（NGF AddPropertyCallBack）**
- MObject 的 dirty tracking 机制已就位，但外部无法订阅
- 增加 `OnPropertyChanged` 回调链，发布 `FPropertyChangedEvent`
- 低风险：现有 dirty 逻辑不变，只在外面包一层

**3. MObject 分层生命周期（NGF NFIModule）**
- NGF 的 `Awake / Init / AfterInit / Execute / Shut` 非常成熟
- MObject 可以在构造、Init、Tick、Destroy 时增加钩子
- 让 Player 子对象系统有更清晰的启动顺序

### 中期可探索（需要规划）

**4. Scene / Group / Cell 三层空间**
- Group 实现副本实例化（主城-1、主城-2）
- Cell 实现 AOI 视野剔除（只同步周围 N 格内的实体变化）
- 影响范围大，需要先梳理 SceneServer 架构

**5. 配置数据系统（NGF NFIElementModule）**
- NGF 从 XML 加载怪物属性、技能表、物品定义
- Mession 目前 `SkillCatalog::LoadBuiltInDefaults()` 是硬编码
- 参考 TODO.md 中"推进数据驱动战斗"

**6. 跨进程 Actor 消息（NFActor + ET Actor）**
- ET 的 Actor 是分布式消息路由
- Mession 当前跨服务器通信走 RPC，Actor 消息是另一层抽象
- 需要配合网络层改造

**7. Plugin 热重载（NGF）**
- NGF 的 `ReLoadPlugin()` 支持运行时重载 DLL
- Mession 全静态链接，当前无此需求
- 长期运营能力，中期不考虑

---

## 五、已确认 Mession 亮点

以下能力在 ET 和 NGF 中都没有或较弱，Mession 已有且值得保持：

- **反射驱动的 RPC + 脏追踪 + 持久化三位一体**：ET 用 C# dynamic，NGF 用 protobuf，Mession 用同一个 `MPROPERTY(PersistentData | Replicated)` 标记同时驱动持久化和复制，代码量最少
- **per-player strand + epoch 防竞态**：ET 和 NGF 都用锁或消息队列，Mession 的 epoch 机制处理 logout/relogin 竞态更干净
- **Fiber 后端抽象（支持 POSIX 和 Windows）**：NGF 没有协程，ET 依赖 C# runtime，Mession 的 `IFiberBackend` 接口更灵活

---

## 六、后续行动建议

1. **阶段一完成**（EventBus + TimerManager + Fiber）后，立即推进 **属性变更事件**
2. **属性变更事件** 完成后，推进 **per-object 事件作用域**
3. **Scene / Group / Cell** 作为独立大项，需要先写专项设计文档
4. 每项改动完成后同步更新 `validate.py`，参考 TODO.md 的"新增主链路能力时同步补进 validate.py"
