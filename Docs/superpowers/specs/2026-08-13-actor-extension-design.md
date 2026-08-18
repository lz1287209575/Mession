# 现有 Actor 抽象扩展为完整 Actor 模型 — 设计 Proposal

> 起草:2026-08-13
> 状态:v1 提案
> 关联:
> - `Docs/superpowers/specs/2026-07-07-actor-rpc-refactor.md`(actor-RPC 重构背景)
> - `Docs/superpowers/specs/2026-08-13-multi-reactor-design.md`(配套:多 Reactor 下 actor 分布)

---

## 0. 一句话

把当前 Mession 的 `MActorRouter` 从"地址簿 + 寻址 RPC"扩展为完整 actor 模型:**新增 `IActor` 接口 + `FActorMessage` 抽象 + `MActorHandle::Post/Call`**,沿用现有 `MServiceId::Make` ActorId 布局、`MRpcChannel::CallToActor` RPC 通道、反射 codegen,业务代码零改动就能编过。

## 1. 目标

1. **扩展 `MActorRouter`**:新增 `RegisterActor(IActor*, SubPool*)` 重载,在现有 `RoutesMutex / ActorRoutes` 旁边加 `ActorsMutex / ActorObjects`。
2. **新增 `IActor` 接口**:`GetActorId()` 沿用 `MServiceId::Make(ServiceType, InstId)` 64-bit 布局;`OnMessage(Msg)` 是消息处理入口。
3. **新增 `FActorMessage` 结构**:`SMessageHeader`(Sender/Target/MsgType/PayloadSize)+ `TByteArray Payload` + 可选 `ReplyPromise`。
4. **新增 `MActorHandle`**:`Post(Msg)` 异步 send;`Call(Msg)` 同步调用(返回 `SFutureResult<TByteArray>`);本进程 Post 到 actor 自己的 Sub ambient,远端走 `MRpcChannel::CallToActor`。
5. **既有 API 零破坏**:`RegisterActor(ActorId, ServerType, ConnId)` / `FindActor` / `IsActorLocal` / `SendToActor` 全部保留,`EchoService::Echo` / `EchoAwait` 反射 RPC 不动。
6. **本轮不动的**:`MEchoService` 不派生 `IActor`(业务代码保持现状);`RegisterLocalActors` 不变;PoC 链路不退化。

## 2. 非目标

- 改 `MEchoService` 实现 `IActor`(留待阶段 2.1)
- 改 `MRpcChannel::CallToActor` 内部(沿用即可,远端 actor 走现有 RPC)
- 跨进程 actor 分布式路由(留待后续 spec)
- actor state 持久化 / 故障恢复(留待后续 spec)
- `EServerType::Unknown` 用法清理(留待阶段 2.2 一起做)
- 重构 `MAsyncContext` ambient 语义(由 multi-reactor spec 处理)
- 新增反射 codegen(`MFunction::ActorMessageHandler`)— 留待阶段 2.2
- actor 消息序列化格式定义(MSTRUCT + MPROPERTY)— 留待阶段 2.2

## 3. 现状基线

### 3.1 已有"actor 骨架"(复用部分)

| 已有组件 | 位置 |复用方式 |
|---|---|---|
| `MServiceId::Make(GetServiceType/GetInstId)` | `Source/Servers/App/ServiceId.h:14-54` | ActorId 64-bit 布局直接复用 |
| `MActorRouter::RegisterActor / FindActor / IsActorLocal` | `Source/Common/Net/Routing/ActorRouter.h:11-105` | 既有路由表保留 |
| `SActorRoute`(ActorId / ServerType / ConnId / LastUpdateTime) | `Source/Common/Net/Routing/ActorRoute.h:6-12` | 保留 |
| `MRpcChannel::CallToActor<TResp>` | `Source/Common/Net/Rpc/MRpcChannel.h:69-79` | 远端 actor 走这条 |
| `MFunction::ServerCallHandler`(反射 dispatch) | `Source/Tools/MHeaderTool/Generation/CodeGenerator.h:639-688` | 本轮不动,阶段 2.2 接 actor 消息 |
| `MEndpointCache::ServiceInstance_`(反射元数据) | `Source/Common/Net/ServiceDiscovery/EndpointCache.h:30-97` | 反射发现机制保留 |
| `MAsyncContext / MLoopAsyncContext` | `Source/Common/Runtime/Async/AsyncContext.h:22-51` | `MActorHandle::Post` 复用 ambient 投递 |

### 3.2 缺失部分(本轮新增)

| 缺失项 | 影响 |
|---|---|
| `IActor` 接口 | 业务 actor 没有统一抽象,实现零散 |
| `FActorMessage` | 跨 actor 调用走直接函数,不是消息 |
| `MActorHandle::Post/Call` | 业务侧没法"发消息",只能调函数 |
| `MActorRouter` actor 对象表 | 寻址表只有"在哪",没有"是谁" |

### 3.3 现状 PoC 链路(本轮不破坏)

```
Gateway ──► MT_FunctionCall ──► EchoService
              │
              ▼
         ServerCall 反射 dispatch
              │
              ▼
         MEchoService::Echo(Request)  ← 既有反射 RPC,本轮保留
              │
              ▼
         FSampleEchoResponse 回包
```

链路:`Scripts/validate.py` chain_local / chain_remote / chain_remote_async / error_unknown 4 套件,必须仍绿。

## 4. 目标架构

### 4.1 模块关系图

```
 ┌─────────────────────────────────────────────────────────────────────┐
 │ Mession Actor 扩展 — 模块关系                │
 │
 │ ┌───────────────────────┐                       │
 │ │ IActor(纯虚接口)        │                       │
 │ │  GetActorId()         │                       │
 │ │  OnMessage(Msg)       │                       │
 │ │  OnCreated()          │                       │
 │ │  OnDestroyed()        │                       │
 │ └───────────┬────────────┘                       │
 │ 实现            │ │
 │ │ │
 │ (本轮不实现,留给业务 actor)  │
 │ │
 │ 持有 │
 │ │ │
 │ ┌────────────▼────────────┐ ┌─────────────────────┐ │
 │ │ MActorHandle              │ │ FActorMessage │ │
 │ │  ActorId / IActor* / Pool│ │  Header / Payload / │
 │ │                           │ │  ReplyPromise     │ │
 │ │  Post(Msg)              │ └─────────────────────┘ │
 │ │  Call(Msg) -> Future │ ▲              │ │
 │ └────────────┬─────────────┘ │              │ │
 │              │ uses           │              │ │
 │              │                │              │ │
 │ ┌────────────▼────────────────────────┐     │ │
 │ │ MActorRouter(扩展)                  │◄────┘ │
 │ │ ┌────────────────────────────────┐ │ │
 │ │ │ 既有:                            │ │ │
 │ │ │  RoutesMutex + ActorRoutes       │ │ │
 │ │ │  (地址簿,SActorRoute)            │ │ │
 │ │ │                                  │ │ │
 │ │ │  RegisterActor(ActorId, Type, C) │ │ │
 │ │ │  FindActor / IsActorLocal        │ │ │
 │ │ └────────────────────────────────┘ │ │
 │ │ ┌────────────────────────────────┐ │ │
 │ │ │ 新增:                            │ │ │
 │ │ │  ActorsMutex + ActorObjects      │ │ │
 │ │ │  (actor 对象表,IActor*)       │ │
 │ │ │  ActorPools(actor → SubPool*)  │ │
 │ │ │                                  │ │ │
 │ │ │  RegisterActor(IActor*, Pool*)  │ │ │
 │ │ │  FindHandle(ActorId)            │ │ │
 │ │ └────────────────────────────────┘ │ │
 │ └─────────────────┬────────────────────┘ │
 │                   │ │
 │ ┌─────────────────▼─────────────────┐ │
 │ │ MSubReactorPool(由 multi-reactor spec) │ │
 │ │  GetAmbient(SubId) → MAsyncContext* │ │
 │ │  Post(SubId, Task)                │ │
 │ └─────────────────┬─────────────────┘ │
 │                   │ │
 │ ┌─────────────────▼─────────────────┐ │
 │ │ MRpcChannel(既有,不改动)          │ │
 │ │  CallToActor(远端 actor 走这条) │ │
 │ └────────────────────────────────────┘ │
 └─────────────────────────────────────────────────────────────────────┘
```

### 4.2 数据流图

#### 本进程 actor 调用

```
 业务侧(任意 Worker / Sub 线程)
   │
   ▼ MActorRouter::Get().FindHandle(ActorId)
   │
 MActorHandle{ Actor, Pool }
   │
   ▼ Handle.Post(Msg)
   │
 Pool->GetAmbient(SubId)->Post([Actor, Msg] { Actor->OnMessage(Msg); })
   │
   ▼ (Post 是异步:函数立即返回)
   │
   ┌───────────────────┐
   │ Sub #SubId I/O 线程(下一帧 TaskLoop drain) │
   │ 执行 Actor->OnMessage(Msg)            │
   │ actor 内部 state 修改(单线程访问安全) │
   └───────────────────┘
```

#### 跨进程 actor 调用

```
 业务侧(任意 Worker / Sub 线程)
   │
   ▼ MActorRouter::Get().FindHandle(ActorId) → 远端 Handle(仅 id)
   │
 Handle.Post(Msg) — 走 MRpcChannel::CallToActor
   │
   ▼ (复用既有 RPC)
   │
 对端 EchoService 进程的 MEchoService::Echo(...)
   │
   ▼ (本轮远端不接 actor 消息,继续走反射 RPC)
   │
   SFutureResult<TResp> 回包
```

## 5. 文件改动

### 5.1 新增文件

| 文件 | 行数 | 说明 |
|---|---|---|
| `Source/Common/Runtime/Actor/IActor.h` | ~50 | `IActor` 接口 |
| `Source/Common/Runtime/Actor/FActorMessage.h` | ~50 | `FActorMessage` 结构 + `SMessageHeader` |

### 5.2 修改文件

| 文件 | 改动 | 说明 |
|---|---|---|
| `Source/Common/Net/Routing/ActorRouter.h` | +50 行 | 新增 `MActorHandle` 类 + `MActorRouter::RegisterActor(IActor*, SubPool*)` 重载 + `FindHandle` 方法 + `ActorsMutex` / `ActorObjects` / `ActorPools` 字段 |
| `Source/Common/Net/Routing/ActorRouter.cpp` | +90 行 | 新增 4 个方法实现:`RegisterActor(IActor*, SubPool*)` / `UnregisterActor` 重载(清理 actor 对象)/ `FindHandle` / `MActorHandle::Post` / `MActorHandle::Call` |

### 5.3 不改文件

- `MEchoService.h` / `EchoService.cpp`(阶段 2 才动)
- `MRpcChannel.h` / `.cpp`(沿用)
- `MEndpointCache.h` / `.cpp`(沿用)
- `MAsyncContext.h` / `.cpp`(沿用)
- `MHeaderTool` codegen(本轮不新增 `ActorMessageHandler` 字段)
- `Scripts/validate.py` / `Scripts/servers.py`(链路不变)

## 7. 详细设计

### 7.1 `IActor` 接口

```cpp
namespace mession::actor
{
    /**
     * @brief IActor - 业务 actor 接口.
     *
     * 实现约束:
     * 1. GetActorId() 必须用 MServiceId::Make(ServiceType, InstId) 返回
     * 2. OnMessage() 永远在 actor 自己的 Sub 线程调用(单线程访问)
     * 3. actor 内部 state 由派生类自己持有,外部不可直接访问
     */
    class IActor
    {
    public:
        virtual ~IActor() = default;
        virtual uint64 GetActorId() const = 0;
        virtual void OnMessage(const FActorMessage& InMsg) = 0;
        virtual void OnCreated() {}
        virtual void OnDestroyed() {}
    };
}
```

**约束**(CodingStyle §7.4 行内注释说明 why):
- `GetActorId` 沿用 `MServiceId::Make` 是为了 **ActorId 布局兼容**(寻址 / 反射 / 序列化都用同一布局)。
- `OnMessage` 在 actor 自己的 Sub 线程调 → actor state 单线程访问安全。
- 不允许外部直接调 `OnMessage`,只能通过 `MActorHandle::Post / Call`。

### 7.2 `FActorMessage` 结构

```cpp
namespace mession::actor
{
    struct SMessageHeader
    {
        MPROPERTY() uint64 SenderId = 0;
        MPROPERTY() uint64 TargetId = 0;
        MPROPERTY() int32 MsgType = 0;
        MPROPERTY() uint32 PayloadSize = 0;
    };

    struct FActorMessage
    {
        SMessageHeader Header;
        TByteArray Payload;
        TSharedPtr<MPromise<TByteArray>> ReplyPromise; // 仅 Call 用

        static FActorMessage MakePost(uint64 From, uint64 To,
                                       int32 Type, TByteArray Payload);
        static MAsync::SFutureResult<TByteArray> MakeCall(uint64 From,
                                                           uint64 To,
                                                           int32 Type,
                                                           TByteArray Payload);
    };
}
```

**约束**:
- 反射字段名 `SenderId / TargetId / MsgType / PayloadSize` 是公开 ABI(CodingStyle §9.1),改名会破坏 wire。
- `ReplyPromise` 是 `TSharedPtr`,允许 `MakeCall` 返回 future 后,`Post` 异步触发的 `OnMessage` 通过 `ReplyPromise->SetValue(...)` 回包。
- `MakeCall` 实现:`MakeShared<MPromise<TByteArray>>()` → `Promise->GetFuture()` → 把 Promise 塞进 `Msg.ReplyPromise` → 返回 future。

### 7.3 `MActorHandle` 类

```cpp
namespace mession::net
{
    class MActorHandle
    {
    public:
        MActorHandle() = default;
        MActorHandle(uint64 InActorId, IActor* InActor,
                      MSubReactorPool* InPool);

        void Post(const FActorMessage& InMsg) const;
        MAsync::SFutureResult<TByteArray> Call(FActorMessage InMsg) const;

        uint64 GetActorId() const { return ActorId; }
        bool IsLocal() const { return Actor != nullptr; }
        IActor* GetActor() const { return Actor; }

    private:
        uint64 ActorId = 0;
        IActor* Actor = nullptr;             // 本进程时有效
        MSubReactorPool* Pool = nullptr;     // 本进程时有效
    };
}
```

**关键实现**:

```cpp
void MActorHandle::Post(const FActorMessage& InMsg) const
{
    if (!IsLocal())
    {
        // 远端 actor — 复用现有 MRpcChannel::CallToActor
        MRpcChannel::Get().CallToActor<TByteArray>(
            ActorId,
            "MEchoService",  // TODO:阶段 2.2 按 actor 类型分发
            "OnMessage",
            SerializeMessage(InMsg));
        return;
    }

    // 本进程:Post 到 actor 自己的 Sub ambient
    const uint32 SubId = ActorId % Pool->GetSubCount();
    MAsyncContext* Ambient = Pool->GetAmbient(SubId);

    // 拷贝消息到 lambda 捕获,避免 caller 释放
    FActorMessage Captured = InMsg;
    Ambient->Post([Actor = this->Actor, Captured]()
    {
        // 这里已经在 actor 自己的 Sub 线程
        Actor->OnMessage(Captured);
    });
}

SFutureResult<TByteArray> MActorHandle::Call(FActorMessage InMsg) const
{
    auto Promise = MakeShared<MPromise<TByteArray>>();
    SFutureResult<TByteArray> Future = Promise->GetFuture();
    InMsg.ReplyPromise = Promise;

    Post(InMsg);
    return Future;
    // 业务侧:Future.Get() / await / .Then(...)
}
```

### 7.4 `MActorRouter` 扩展

新增字段:
```cpp
class MActorRouter
{
private:
    // 既有
    mutable std::mutex RoutesMutex;
    TMap<uint64, SActorRoute> ActorRoutes;

    // 新增
    mutable std::mutex ActorsMutex;
    TMap<uint64, TUniquePtr<IActor>> ActorObjects;
    TMap<uint64, MSubReactorPool*> ActorPools;
};
```

新增方法:
```cpp
// 既有
void RegisterActor(uint64 ActorId, EServerType ServerType, uint64 ConnId = 0);
SActorRoute FindActor(uint64 ActorId) const;
bool IsActorLocal(uint64 ActorId) const;

// 新增
void RegisterActor(IActor* InActor, MSubReactorPool* InPool);
void UnregisterActorObject(uint64 ActorId); // 既有 UnregisterActor 保留,新增清理对象
MActorHandle FindHandle(uint64 ActorId);
```

`RegisterActor(IActor*, SubPool*)` 实现:

```cpp
void MActorRouter::RegisterActor(IActor* InActor, MSubReactorPool* InPool)
{
    if (InActor == nullptr)
    {
        LOG_FATAL("MActorRouter::RegisterActor with null IActor");
        std::abort();
    }

    const uint64 ActorId = InActor->GetActorId();

    // 既有路由表:填 SActorRoute(本进程用 ServerType=Unknown hack 标记)
    SActorRoute Route;
    Route.ActorId = ActorId;
    Route.ServerType = EServerType::Unknown;
    Route.ConnectionId = (InPool != nullptr) ? (ActorId % InPool->GetSubCount()) : 0;
    Route.LastUpdateTime = static_cast<uint64>(NowSeconds());

    {
        std::lock_guard<std::mutex> Lock(RoutesMutex);
        ActorRoutes[ActorId] = Route;
    }

    // 新增 actor 对象表
    {
        std::lock_guard<std::mutex> Lock(ActorsMutex);
        if (ActorObjects.Contains(ActorId))
        {
            LOG_WARN("actor %llu already registered, replacing", ActorId);
        }
        ActorObjects[ActorId] = TUniquePtr<IActor>(InActor);
        ActorPools[ActorId] = InPool;
    }

    // 在 actor 自己的 Sub 线程回调 OnCreated
    if (InPool != nullptr)
    {
        const uint32 SubId = ActorId % InPool->GetSubCount();
        MAsyncContext* Ambient = InPool->GetAmbient(SubId);
        if (Ambient != nullptr)
        {
            Ambient->Post([InActor] { InActor->OnCreated(); });
        }
    }
}
```

### 7.5 与 `MSubReactorPool` 的接口依赖

`MActorHandle::Post` 需要 `MSubReactorPool::GetAmbient(SubId)` 接口。这个接口**不在本 spec 引入**(由 `multi-reactor-design.md` 引入)。

**临时方案**:本 spec 实现里,如果 `Pool == nullptr` 或 `GetAmbient` 不可用,fall back 到**直接调 actor**(绕过 Sub 投递)—— 用于本轮"骨架就位但 SubPool 还没接"阶段。

```cpp
// 临时 fallback(阶段 1 用)
if (Ambient == nullptr)
{
    LOG_WARN("SubPool not ready, executing OnMessage on caller's thread");
    Actor->OnMessage(InMsg);
    return;
}
```

`MSubReactorPool` 实装后(由 multi-reactor spec 引入),删除 fallback。

## 8. 实施步骤

### 阶段 1.1 — 新增 IActor 接口
- 新建 `Source/Common/Runtime/Actor/IActor.h`
- 新建 `Source/Common/Runtime/Actor/FActorMessage.h`
- 验证:`cmake --build Build -j4`(不破坏编译)

### 阶段 1.2 — 扩展 MActorRouter
- 改 `Source/Common/Net/Routing/ActorRouter.h` 加新字段 + 新方法 + `MActorHandle`
- 改 `Source/Common/Net/Routing/ActorRouter.cpp` 加新方法实现
- 既有 API 一字不动
- 验证:`cmake --build Build -j4`

### 阶段 1.3 — MActorHandle::Post/Call 实现
- 复用 `MRpcChannel::CallToActor`(远端路径)
- 临时 fallback:本进程直接调 `Actor->OnMessage`(等 SubPool 就位)
- 验证:`cmake --build Build -j4`

## 9. 验证

按用户指定,**仅需 cmake build 绿**。

```bash
cmake --build Build -j4
```

**要求**:
- ✅ 不破坏现有编译
- ✅ 不破坏现有 PoC 链路(虽然本轮不跑 `validate.py`,但代码层面不能引入回归)
- ✅ 不破坏 `Scripts/check-style.sh`(新增代码符合 `Docs/CodingStyle.md`)

## 10. 风险与限制

| 风险 | 缓解 |
|---|---|
| `MActorHandle::Post` 临时 fallback 会在 caller 线程直接调 `OnMessage`(阶段 1 等 SubPool 阶段) | 文档明示,阶段 1.3 完成时加 `LOG_WARN`;SubPool 就位后删除 fallback |
| 远端 actor 走 `MRpcChannel::CallToActor`,但对端没接 actor 消息路由 | 当前 stub,远端调用实际无效 — 文档明示,阶段 2.2 一起接 |
| `SMessageHeader` 字段名是 ABI(CodingStyle §9.1) | 字段名固定 `SenderId / TargetId / MsgType / PayloadSize`,不重排 |
| 新增 `IActor` 抽象可能跟未来 MAsyncContext 改造冲突 | 由 multi-reactor spec 处理,本 spec 只搭骨架 |
| `MSubReactorPool` 还没实装 | 临时 fallback 兜底,不阻塞本轮验证 |

## 11. 后续工作

- 阶段 2:改 `MEchoService` 派生 `IActor` + 实现 `OnMessage`
- 阶段 3:第一个业务 actor(排行榜 / 公会)落地,验证完整链路
- 阶段 4:`MRpcChannel::CallToActor` 识别 actor 消息路由
- 阶段 5:actor state 持久化 + 故障恢复
- 阶段 6:跨进程 actor + 分布式路由

每个阶段独立 PR,独立验证。

## 12. 关联 spec

- `Docs/superpowers/specs/2026-08-13-multi-reactor-design.md` — 多 Reactor 设计(本 spec 的 SubPool 来源)
- `Docs/superpowers/specs/2026-07-07-actor-rpc-refactor.md` — actor-RPC 重构背景(本 spec 的前置)
- `Docs/superpowers/specs/2026-07-24-cpp17-async-await.md` — `SFutureResult` 契约(`Call` 返回类型)
- `Docs/superpowers/specs/2026-07-13-service-registry-design.md` — `MRpcChannel::CallToActor` 来源