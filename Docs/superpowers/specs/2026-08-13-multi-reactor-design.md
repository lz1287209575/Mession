# 多线程 Reactor 设计 Proposal

> 起草:2026-08-13
> 状态:v1 提案
> 关联:
> - `Docs/superpowers/specs/2026-08-13-actor-extension-design.md`(配套:多 Reactor 下 actor 分布)
> - `Docs/superpowers/specs/2026-07-24-cpp17-async-await.md`(`SFutureResult` / ambient 契约)

---

## 0. 一句话

把当前单 Reactor(`MNetEventLoop`)扩展为 **主从 Reactor 池**:**主 acceptor 单 I/O 线程 + N 个 Sub reactor + M 个 Worker 池**。Sub reactor 跑 I/O(`poll` + `recv` + `send`),Worker 跑业务(反射 dispatch + OnMessage)。**actor 状态单线程访问**由 `MSubReactorPool::GetAmbient(SubId)` 显式 ambient 保障。

## 1. 目标

1. **`MSubReactorPool`**:持有 N 个 `MNetEventLoop` + N 个 `MTaskEventLoop` + N 个 `MAsyncContext`,各自独立线程运行;`PickSub(addr) → SubId` 用 `StableHash(addr) % N`。
2. **`MWorkerPool`**:M 个业务 worker 线程 + 一个 MPMC 任务队列(用 `std::mutex + std::condition_variable` 实现);`Start(N)` / `Stop()` / `Enqueue(Task)`。
3. **`MAcceptorReactor`**:单 I/O 线程的 acceptor + fd 派发器;`OnAccept(conn)` 后通过 `SubPool->Post(SubId, ...)` 跨线程让目标 Sub 注册 fd。
4. **`MEndpointCache` 分桶**:ConnectionPool 从全局一把锁改为 per-Sub 桶(`TMap<uint32, SPerSubPool>`),锁粒度降到 Sub 数。
5. **actor 在 Sub 上的分布**:`MActorHandle::Post(本地 actor)` → `Pool->GetAmbient(ActorId % SubCount)->Post(...)` → 强制 actor 状态访问在单一 Sub 线程。
6. **`MAsyncContext` ambient 沿用**:`GCurrentContext` TLS 不动,Worker 线程 GCurrentContext 保持为 nullptr,异步续体显式 capture SubAmbient。
7. **Reactor 模型选择**:**Reactor(同步 I/O 复用)** 而非 Proactor(异步 I/O);理由见 §11。

## 2. 非目标

- 切到 epoll/kqueue/IOCP(本轮仍用 `poll`,跨平台稳定;真上生产才切 epoll)
- Proactor 模型(IOCP / io_uring)— 与业务逻辑密度不契合
- 跨进程 actor 分布式路由
- actor 状态持久化 / 故障恢复
- Registry HA / 多实例
- 协程 / fiber 化(沿用现有 C++17 async/await)

## 3. 现状基线

### 3.1 单 Reactor 现状

| 组件 | 位置 | 现状 |
|---|---|---|
| `MNetEventLoop` | `Source/Common/Runtime/EventLoop/NetEventLoop.cpp:55-190` | 单线程 `poll` + dispatch |
| `MNetServerBase` | `Source/Common/Net/NetServerBase.cpp:6-58` | 1 主循环 + 1 TaskLoop + 1 NetLoop |
| `MEndpointCache::ConnectionPool_` | `Source/Common/Net/ServiceDiscovery/EndpointCache.h:30-97` | 全局单锁,所有 Sub(将来)/ 当前单实例共享 |
| `MAsyncContext::GCurrentContext` | `Source/Common/Runtime/Async/AsyncContext.cpp:10` | TLS 单 ambient |
| `MServerConnection` | `Source/Common/Net/ServerConnection.cpp:1-266` | 单连接管理 + 心跳 + 重连 |
| `MSocket` + `MSocketPlatform` | `Source/Common/IO/Socket/Socket.cpp:1-398` | poll-based,跨平台 WSAPoll / poll |
| `MFiberScheduler` | `Source/Common/Runtime/Concurrency/FiberScheduler.h:122-146` | 仅 player-command 基础设施用,业务不依赖 |

### 3.2 现状痛点

- **单 I/O 线程**:所有 fd + 所有业务回调都在同一线程,串行处理,慢业务卡整个 event loop
- **连接池单锁**:高并发下 `GetOrConnect` 锁竞争严重
- **业务在 I/O 线程**:业务体直接调,业务慢 → I/O 慢 → 所有 conn 慢
- **actor 单线程契约无强制**:PoC 阶段业务简单不暴露,真做业务会崩
- **跨线程 ambient 语义模糊**:Worker 线程 GCurrentContext = nullptr,异步续体 Post 走不通

### 3.3 真生产规模假设(规划目标)

| 指标 | 当前 PoC | 目标(单进程)|
|---|---|---|
| 并发 UE 连接 | <10 | 5000-10000 |
| 单 conn 业务 P99 | <10ms | <50ms |
| Sub reactor 数 | 1 | `hardware_concurrency()` |
| Worker 池大小 | 0(业务在 I/O) | `hardware_concurrency() * 2` |
| 单 fd buffer 上限 | 65535B(不变) | 65535B |
| 系统总 fd | <10 | 10000+ |

## 4. 目标架构

### 4.1 整体架构图

```
 ┌────────────────────────────────────────────────────────────────┐
 │ 主 acceptor(1 个 I/O 线程)                          │
 │ ┌────────────────────────────┐                          │
 │ │ MAcceptorReactor        │                          │
 │ │ listen + accept            │                          │
 │ │ OnAccept → 派发到 Sub  │                          │
 │ └──────────────┬─────────────────┘                          │
 │                │ hash(remote_addr) % N                          │
 │ ┌────────────────┼─────────────────┐                          │
 │ ▼ ▼ ▼ │
 │ ┌───────────┐ ┌───────────┐ ┌───────────┐ ┌───────────┐ │
 │ │ Sub #0  │ │ Sub #1  │ │ Sub #2  │ │ Sub #N  │ │
 │ │MNetEventLoop│ MNetEventLoop│ MNetEventLoop│ MNetEventLoop│ │
 │ │ (CPU #0) │ │ (CPU #1) │ │ (CPU #2) │ │ (CPU #N) │ │
 │ │ ┌─────┐ │ │ ┌─────┐ │ │ ┌─────┐ │ │ ┌─────┐ │ │
 │ │ │ambient0│ │ │ambient1│ │ │ambient2│ │ │ambientN│ │ │
 │ │ └─────┘ │ │ └─────┘ │ │ └─────┘ │ │ └─────┘ │ │
 │ │ fd集 A  │ │ fd集 B  │ │ fd集 C  │ │ fd集 N  │ │
 │ └─────┬─────┘ └─────┬─────┘ └─────┬─────┘ └─────┬─────┘ │
 │ │ │ │ │ │
 │ OnRead → 投到 Worker 池                    │
 │ │ │ │ │ │
 │ ▼ ▼ ▼ ▼ │
 │ ┌──────────────────────────────────────────────────────────┐ │
 │ │ MWorkerPool(M 个线程, MPMC queue)                       │ │
 │ │ ┌────┐ ┌────┐ ┌────┐ ┌────┐ ┌────┐ ...                │ │
 │ │ │W#0 │ │W#1 │ │W#2 │ │W#3 │ │W#4 │                    │ │
 │ │ └────┘ └────┘ └────┘ └────┘ └────┘                    │ │
 │ │                                                          │ │
 │ │ - 反射 dispatch + 业务逻辑                                │ │
 │ │ - actor OnMessage(在 actor 自己的 Sub ambient 调)   │ │
 │ │ - 序列化 response + Post 回 Sub 写包                │ │
 │ └──────────────────────────────────────────────────────────┘ │
 │ │ │
 │ MAsync::Post 回原 Sub │
 │ ▼ │
 │ 各 Sub 写 conn 的 SendBuffer │
 └────────────────────────────────────────────────────────────────┘
```

### 4.2 actor 分布

```
 ┌────────────────────────────────────────────────────────────────┐
 │ Actor 分布策略:hash(ActorId) % SubCount                       │
 │ │
 │ 业务启动时:                                                 │
 │ MActorRouter::RegisterActor(MyActor, SubPool)              │
 │ │                                                        │
 │ ├─► 选 SubId = MyActor.GetActorId() % SubPool.GetSubCount()│
 │ ├─► 填 ActorRoutes(既有)                                    │
 │ ├─► 存 ActorObjects + ActorPools(本 spec 扩展)            │
 │ └─► 在 Sub #SubId 调 OnCreated                                │
 │ │
 │ 业务访问 actor:                                                │
 │ Handle = MActorRouter::FindHandle(ActorId)                  │
 │ Handle.Post(Msg)                                              │
 │ │                                                          │
 │ └─► SubAmbient = Pool->GetAmbient(ActorId % SubCount) │
 │ └─► SubAmbient->Post([Actor, Msg] { Actor->OnMessage(Msg); }); │
 │                                                              │
 │ 关键保证:同一 ActorId 永远在同一 SubId(actor 状态单线程访问)│
 └────────────────────────────────────────────────────────────────┘
```

### 4.3 与 actor-extension 的关系

```
┌─────────────────────────────────────────────────────────────────┐
│ actor-extension spec 提供: │
│ - IActor / FActorMessage / MActorHandle │
│ - MActorRouter 扩展(actor 对象表)                  │
│ │
│ multi-reactor spec 提供:                                                  │
│ - MSubReactorPool(GetAmbient / Post / PickSub / GetLoop)│
│ - MWorkerPool(Enqueue / Start / Stop)               │
│ - MAcceptorReactor(Start / Stop / OnAccept)            │
│ - MEndpointCache per-Sub 分桶                                          │
│ │
│ 两者协同: │
│ - MActorHandle::Post 本进程路径 用 MSubReactorPool::GetAmbient │
│ - MActorHandle::Post 远端路径 仍走 MRpcChannel::CallToActor │
└─────────────────────────────────────────────────────────────────┘
```

## 5. 模块改动清单

### 5.1 新增文件

| 文件 | 行数(估)| 说明 |
|---|---|---|
| `Source/Common/Runtime/Concurrency/SubReactorPool.h` | ~120 | `MSubReactorPool` 声明 |
| `Source/Common/Runtime/Concurrency/SubReactorPool.cpp` | ~110 | Init / Shutdown / PickSub / Post / GetAmbient / GetLoop |
| `Source/Common/Runtime/Concurrency/WorkerPool.h` | ~80 | `MWorkerPool` 声明 |
| `Source/Common/Runtime/Concurrency/WorkerPool.cpp` | ~110 | Start / Stop / Enqueue / WorkerLoop |

### 5.2 修改文件

| 文件 | 改动 | 说明 |
|---|---|---|
| `Source/Common/Net/ServiceDiscovery/EndpointCache.h` | +30 行 | `SPerSubPool` 结构 + `GetOrConnect(Type, SubId)` 重载 + `SetServiceInstance(SubId, ...)` / `GetServiceInstance(SubId)` |
| `Source/Common/Net/ServiceDiscovery/EndpointCache.cpp` | +50 行 | per-Sub 懒创建 + 加锁分桶 |
| `Source/Servers/App/ServiceMain.h` | +30 行 | 多 Reactor 启动 / 关闭 / SubPool 注入到 MEndpointCache / MActorRouter |

### 5.3 不改文件

- `MNetEventLoop.h` / `.cpp`(内部逻辑不变,只是从 1 个实例变 N 个实例)
- `MAsyncContext.h` / `.cpp`(TLS ambient 不动,新增 `MEndpointCache::SetServiceInstance` per-Sub 持有 ambient 副本供 Worker 查)
- `MHeaderTool` codegen(本轮不动,actor 消息路由留阶段 2.2)
- `MRpcChannel` / `RpcServerCall`(沿用,ServerCall 反射 dispatch 不变)
- `Scripts/validate.py` / `Scripts/servers.py`(链路逻辑不变)

## 6. 详细设计

### 6.1 `MSubReactorPool`

```cpp
namespace mession::net
{
    class MSubReactorPool
    {
    public:
        void Init(uint32 InSubCount);
        void Shutdown();

        uint32 PickSub(const MString& InRemoteAddr) const;
        uint32 PickSub(uint64 InActorId) const;

        void Post(uint32 InSubId, TFunction<void()> InTask);

        MAsyncContext* GetAmbient(uint32 InSubId) const;
        MNetEventLoop* GetLoop(uint32 InSubId) const;

        uint32 GetSubCount() const { return SubCount; }

    private:
        uint32 SubCount = 0;
        TVector<TSharedPtr<MNetEventLoop>> SubLoops;
        TVector<TSharedPtr<MTaskEventLoop>> SubTaskLoops;
        TVector<TSharedPtr<MAsyncContext>> SubAmbients;
        TVector<TSharedPtr<std::thread>> LoopThreads;
        TVector<TSharedPtr<std::thread>> TaskThreads;
        std::atomic<bool> bRunning{false};
    };
}
```

**关键约束**:
- `Init` 时如果 `InSubCount == 0` → `LOG_FATAL + std::abort()`(CodingStyle §5.3)
- `PickSub(addr) = StableHash(addr) % SubCount` — `StableHash` 与 `Reflect/Class.h:18` 同实现(FNV-1a 32 → 折叠 16)→ 这里直接 `% N`
- `Post(SubId, Task)` 必须显式指定 SubId,不允许"任意 Sub"语义
- `GetAmbient(SubId)` 是 `MActorHandle::Post` 的关键依赖

### 6.2 `MWorkerPool`

```cpp
namespace mession::concurrency
{
    class MWorkerPool
    {
    public:
        void Start(uint32 InWorkerCount);
        void Stop();
        void Enqueue(TFunction<void()> InTask);
        uint32 GetWorkerCount() const;

    private:
        void WorkerLoop();

        TQueue<TFunction<void()>> TaskQueue;
        std::mutex QueueMutex;
        std::condition_variable QueueCv;
        TVector<TSharedPtr<std::thread>> Workers;
        uint32 WorkerCount = 0;
        std::atomic<bool> bRunning{false};
    };
}
```

**关键约束**:
- Worker 数量 = `hardware_concurrency() * 2`(业务 CPU-bound)
- WorkerLoop 用 `wait_for(1ms)` 短超时实现"近实时 + 不忙等"
- 业务 lambda 抛异常必须捕获(`try / catch`),不应杀掉 Worker 线程

### 6.3 `MAcceptorReactor`(本轮**不实装**,只留接口位)

**理由**:本轮聚焦在 Sub / Worker /  + 分桶,**主 acceptor 沿用现有 `MNetServerBase` 的单 I/O 线程**。后续 PR 把 `MNetServerBase` 拆成 `MAcceptorReactor + SubPool`。

具体改动:
- 本轮**保留** `MNetServerBase::Run()` 主循环 + ambient 安装
- 后续 PR 把 acceptor 拆出,主 reactor 派发到 Sub

### 6.4 `MEndpointCache` per-Sub 分桶

```cpp
struct SPerSubPool
{
    TMap<uint64, TSharedPtr<MServerConnection>> Connections;
    std::mutex Mutex;
    uint32 RoundRobinIdx = 0;

    MServerConnection* GetOrConnect(EServerType InType, MEndpointCache* InCache);
};

class MEndpointCache
{
public:
    // 既有 API 保留
    MServerConnection* GetOrConnect(EServerType InType); // 单 Sub 兼容

    // 新增 per-Sub API
    MServerConnection* GetOrConnect(EServerType InType, uint32 InSubId);

    IService* GetServiceInstance(uint32 InSubId);
    void SetServiceInstance(uint32 InSubId, IService* InInstance);

private:
    TMap<uint32, IService*> PerSubService;       // 反射上下文 per Sub
    TMap<uint32, SPerSubPool> PerSubPools;        // 连接池 per Sub
};
```

**关键约束**:
- 锁粒度从全局锁降到 per-Sub 锁
- 同一 ServerId 可能出现在多个 Sub 的桶里(因为池独立)
- `PerSubService` 在启动期由 `MServiceMain` 一次性注入,运行期只读

### 6.5 与 actor-extension 的衔接点

`MSubReactorPool::GetAmbient(SubId)` 是 `MActorHandle::Post` 的关键依赖:

```cpp
// actor-extension spec 的 MActorHandle::Post 实现
void MActorHandle::Post(const FActorMessage& InMsg) const
{
    if (!IsLocal())
    {
        MRpcChannel::Get().CallToActor<TByteArray>(ActorId, ...); // 远端
        return;
    }

    // 本进程路径(本 spec 实装后)
    const uint32 SubId = ActorId % Pool->GetSubCount();
    MAsyncContext* Ambient = Pool->GetAmbient(SubId);  // ← 本 spec 提供
    Ambient->Post([Actor = this->Actor, Captured = InMsg]() {
        Actor->OnMessage(Captured);
    });
}
```

**本 spec 阶段**:`MSubReactorPool` 实装后,`MActorHandle` 删除临时 fallback(直接调 OnMessage)。

## 7. 改造顺序(分阶段 PR)

### 阶段 1:`MSubReactorPool` 单类
- 新增 2 个文件
- 引入 `N=1` 的 Sub 实例(等同单 Reactor,验证不退化)
- 验证:`cmake --build Build -j4`

### 阶段 2:`MWorkerPool`
- 新增 2 个文件
- 业务 `OnRead` 投递到 Worker 池(自己不处理)
- 验证:`cmake --build`

### 阶段 3:`MEndpointCache` 分桶 + `SetServiceInstance` per-Sub
- 改 `EndpointCache.h` / `.cpp`
- 锁粒度从全局降到 per-Sub
- 验证:`cmake --build`

### 阶段 4:`MNetServerBase` 装配主从
- 改 `MNetServerBase::Run` 拆 acceptor + Sub
- 引入 `MAcceptorReactor`(如 §6.3 决定)
- 验证:`cmake --build` + `Scripts/validate.py --no-build`

每个阶段独立 PR,独立可测,可回滚。

## 8. 验证(按用户要求)

**仅需 cmake build 绿**:

```bash
cmake --build Build -j4
```

**要求**:
- ✅ 不破坏现有编译
- ✅ 新增代码符合 `Docs/CodingStyle.md`(clang-format 跑通)
- ✅ 不破坏 `Scripts/check-style.sh` 反射 ABI 守卫

**不要求**(本轮):
- ❌ `Scripts/validate.py` 4 套件仍绿(阶段 4 才验证运行时)
- ❌ 性能 / 压测

## 9. 关键决策汇总

| # | 决策 | 选项 | 选择 | 理由 |
|---|---|---|---|---|
| 1 | Reactor vs Proactor | Reactor / Proactor | **Reactor** | 跨平台 + 业务密度高 + 调试可观测性 |
| 2 | fd 分发策略 | round-robin / hash(addr)| **hash(remote_addr)** | session affinity + 缓存命中 |
| 3 | Sub 数量 | 1 / N=CPU / 配置 | **N=hardware_concurrency()** | 默认合理,允许覆盖 |
| 4 | Worker 数量 | CPU / CPU*2 | **CPU*2** | 业务 CPU-bound |
| 5 | 业务回调线程 | Sub I/O / Worker | **Worker** | Sub 不被业务阻塞 |
| 6 | Pending 续体回写 | 自动 / 显式 Post | **显式 Post** | 不依赖 TLS ambient |
| 7 | Worker ambient | TLS / nullptr | **nullptr** | 强制显式传 SubAmbient |
| 8 | ConnectionPool 锁 | 全局 / per-Sub | **per-Sub** | 锁竞争降为 1/N |
| 9 | 主 acceptor | 单 I/O / 复用 Sub #0 | **单 I/O**(本轮)| 简化,后续优化 |

## 10. 风险与限制

| 风险 | 缓解 |
|---|---|
| hash 不均导致某 Sub 过载 | 真生产加虚拟节点 / 一致性 hash |
| `StableHash` 现有实现是 FNV-1a 32 → 折叠 16 位(碰撞可能)| 当前 PoC actor 数<10,碰撞概率低;真生产可换 64 位 hash |
| Worker 数量写死 CPU*2 | 留作 `MService` 配置项,启动期覆盖 |
| 主 acceptor 单点 | 接受,accept 流量小;后续可扩多 acceptor |
| `MEndpointCache::ServiceInstance_` 单例改 per-Sub 是一次性大改 | 启动期注入 + 运行期只读,易测 |
| `MSubReactorPool::Init(0)` 必须 abort | `LOG_FATAL + std::abort()`(CodingStyle §5.3)|
| `MAsyncContext` GCurrentContext TLS 在 Worker 线程是 nullptr | 文档明示,业务代码必须显式拿 SubAmbient |

## 11. Reactor vs Proactor 选择(详细论证)

### 11.1 两个模型对比

**Reactor**:
- 应用调 `read(fd, buf, len)` 同步系统调用
- 内核通知 "fd 可读",应用自己 read
- 数据拷贝在用户态
- 例子:`select` / `poll` / `epoll` / `kqueue`

**Proactor**:
- 应用提交 `ReadFile(fd, buf, len, OVERLAPPED)` 异步 I/O
- 内核全程异步完成(数据已拷到 buf)
- 完成事件投递到 completion queue
- 例子:`IOCP`(Windows) / `io_uring`(Linux 5.1+)

### 11.2 选 Reactor 的理由

| 维度 | Reactor | Proactor |
|---|---|---|
| 跨平台 | ✅ poll 跨 Win/POSIX/macOS | ❌ IOCP 仅 Windows,io_uring 仅 Linux 5.1+ |
| 延迟稳定性 | ✅ 同步 read() 延迟可预测 | ⚠️ OS 调度不可控 |
| 包小(游戏服务器)| ✅ read() 拷贝 64B ~ 几µs 常量 | ⚠️ Proactor 优势在大包 |
| 业务密度(游戏)| ✅ 业务逻辑重,I/O 异步收益边际 | ❌ 业务逻辑吃掉 I/O 收益 |
| 调试可观测性 | ✅ "读到包了,做这事"线性叙事 | ❌ 回调切碎 stack |
| 与 SFutureResult 契合 | ✅ 一层异步(业务)| ❌ 两层异步(I/O + 业务)|

### 11.3 工业界参考

| 框架 | 模型 |
|---|---|
| Skynet(云风)| Reactor |
| KBEngine | Reactor |
| UE Dedicated Server | Reactor |
| Unity Mirror | Reactor(主线程 + 网络线程)|
| Redis | Reactor |
| Nginx(高并发)| Proactor |
| Cloudflare / 高吞吐 CDN | Proactor |
| Tokio / actix(Rust)| Proactor 风格 async I/O |

游戏服务器列**清一色 Reactor**。

### 11.4 多 Reactor 是 Reactor 模型的扩展

多 Reactor **不是 Proactor**:
- 仍然 `poll` / `epoll` 监听 fd(同步事件)
- 仍然 `read` / `recv` 拷贝数据(用户态)
- 只是把"单线程处理"扩成"多线程并行处理"
- 与游戏服务器的契合度与单 Reactor 一致

## 12. 与 actor-extension 的协作时序

```
 ┌─────────────────────────┐
 │ 项目 │ │
 ├─────────────────────────┤
 │ actor-extension spec     │ ◄── 本 spec 之前:搭 IActor + MActorHandle 骨架 │
 │ (本轮) │ │
 ├─────────────────────────┤
 │ multi-reactor spec(本 spec) │ ◄── 本 spec 提供 MSubReactorPool::GetAmbient │
 │ (本轮) │ │
 ├─────────────────────────┤
 │ MActorHandle::Post 实装 │ ◄── 后续阶段,删除临时 fallback │
 │ (阶段 2) │ │
 ├─────────────────────────┤
 │ MEchoService 改 IActor │ ◄── 后续阶段 │
 │ (阶段 2) │ │
 └─────────────────────────┘
```

**本 spec 与 actor-extension spec 是平行进行**:两个都搭骨架,业务侧 0 改动,`cmake --build` 必须绿。

## 13. 后续工作

- 阶段 5:`MNetServerBase` 拆分 acceptor + Sub
- 阶段 6:`MRpcChannel::CallToActor` 识别 actor 消息路由(配合 actor-extension spec 阶段 2)
- 阶段 7:`MEchoService` 派生 `IActor` + 第一个业务 actor(排行榜)
- 阶段 8:POLLOUT 监测(解决 SendBuffer 积压)
- 阶段 9:epoll/kqueue 替换 poll(单 fd 数突破 5000 时)
- 阶段 10:指标化(注册数 / 连接池大小 / 心跳超时次数)

每个阶段独立 PR,独立验证。

## 14. 关联 spec

- `Docs/superpowers/specs/2026-08-13-actor-extension-design.md` — actor 抽象扩展
- `Docs/superpowers/specs/2026-07-24-cpp17-async-await.md` — `SFutureResult` 契约 + ambient 语义
- `Docs/superpowers/specs/2026-07-13-service-registry-design.md` — `MRpcChannel::CallToActor` 来源
- `Docs/Architecture.md`(若存在)— 整体架构背景